// Archive preflight checks.
//
// The whole level-2 idea rests on one claim: for a ZIP or a RAR, the first and
// the last piece are enough to list what the archive contains. These checks
// prove it against the real fixtures by building a file that carries only that
// head and tail, listing it, and comparing the names with the full archive.
// The piece arithmetic is checked separately, because a wrong range would
// quietly fetch the wrong bytes.
#include "ArchivePreflight.h"
#include "ArchiveInspector.h"
#include "SecurityTypes.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const std::string& what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> namesOf(const std::vector<ArchiveInspector::Entry>& entries) {
    std::vector<std::string> names;
    for (const ArchiveInspector::Entry& entry : entries) names.push_back(entry.path);
    return names;
}

fs::path scratch(const std::string& name) {
    return fs::temp_directory_path() / ("ftorrent-preflight-" + name);
}

} // namespace

int main() {
    section("piece range");
    {
        // A file that fits inside the first piece.
        const auto r = ArchivePreflight::computeRange(0, 100, 16384, 100);
        ok(r.firstPiece == 0 && r.firstOffset == 0 && r.firstLength == 100,
           "a small file is one piece");
        ok(r.lastPiece == 0 && r.lastOffset == 0 && r.lastLength == 100,
           "a small file's last piece is the same piece");
    }
    {
        // A file aligned to a piece boundary and spanning three pieces.
        const auto r = ArchivePreflight::computeRange(16384, 40000, 16384, 200000);
        ok(r.firstPiece == 1, "the first piece is the one holding the first byte");
        ok(r.firstOffset == 0, "an aligned file starts at offset 0 in its piece");
        ok(r.firstLength == 16384, "the first piece is read whole");
        ok(r.lastPiece == 3, "the last piece is the one holding the last byte");
        ok(r.lastOffset == 0, "the last piece starts with file bytes here");
        ok(r.lastLength == 40000 - 2 * 16384,
           "the last piece holds only the file's tail");
    }
    {
        // A file that starts in the middle of a piece.
        const auto r = ArchivePreflight::computeRange(20000, 1000, 16384, 100000);
        ok(r.firstPiece == 1 && r.firstOffset == 20000 - 16384,
           "a file that starts mid-piece is sliced at its own offset");
        ok(r.firstLength == 1000, "the whole file fits in that piece");
        ok(r.lastPiece == r.firstPiece && r.lastOffset == r.firstOffset,
           "first and last piece are the same when the file is small");
    }
    {
        const auto r = ArchivePreflight::computeRange(0, 0, 16384, 0);
        ok(r.firstLength == 0 && r.lastLength == 0,
           "an empty file produces no ranges");
    }

    section("sparse assembly");
    {
        ArchivePreflight::Snippet snippet;
        snippet.fileSize = 100000;
        snippet.head = "HEAD";
        snippet.tail = "TAIL";
        const fs::path path = scratch("sparse.bin");
        std::string error;
        ok(ArchivePreflight::writeSparseFile(path.string(), snippet, error),
           "the sparse file is written: " + error);
        ok(fs::file_size(path) == 100000,
           "the sparse file has the original length");

        const std::string bytes = readFile(path);
        ok(bytes.size() == 100000, "the bytes read back match the length");
        ok(bytes.compare(0, 4, "HEAD") == 0, "the head lands at the start");
        ok(bytes.compare(99996, 4, "TAIL") == 0, "the tail lands at the end");
        ok(bytes[50000] == '\0', "the middle is left empty");
        std::error_code ec;
        fs::remove(path, ec);
    }

#ifdef FTORRENT_HAVE_LIBARCHIVE
    section("head and tail are enough to list an archive");
    {
        struct Case {
            const char* file;
            bool expectOk;
        };
        // 64 KiB each way is one piece at the smallest v2 piece size and far
        // more than any of these fixtures needs.
        const std::size_t WINDOW = 64 * 1024;
        const Case cases[] = {
            {"setup.zip", true},
            {"test_read_format_rar5_stored.rar", true},
            {"test_read_format_rar5_compressed.rar", true},
        };

        for (const Case& c : cases) {
            const fs::path full = fs::path(FTORRENT_ARCHIVE_FIXTURES) / c.file;
            const std::string bytes = readFile(full);
            ok(!bytes.empty(), std::string("fixture is present: ") + c.file);
            if (bytes.empty()) continue;

            ArchivePreflight::Snippet snippet;
            snippet.fileSize = static_cast<std::int64_t>(bytes.size());
            snippet.head = bytes.substr(0, std::min(WINDOW, bytes.size()));
            snippet.tail = bytes.substr(bytes.size() > WINDOW ? bytes.size() - WINDOW : 0);

            const fs::path partial = scratch(std::string("partial-") + c.file);
            std::string error;
            ok(ArchivePreflight::writeSparseFile(partial.string(), snippet, error),
               std::string("the partial archive is written: ") + c.file);

            const ArchiveInspector::ListingAll fromPartial =
                ArchiveInspector::listEntries(partial.string());
            const ArchiveInspector::ListingAll fromFull =
                ArchiveInspector::listEntries(full.string());

            ok(fromFull.ok, std::string("the full archive lists: ") + c.file);
            ok(fromPartial.ok == c.expectOk,
               std::string("the partial archive lists as expected: ") + c.file);
            if (fromPartial.ok && fromFull.ok) {
                ok(namesOf(fromPartial.entries) == namesOf(fromFull.entries),
                   std::string("the head and tail see the same entries as the "
                               "whole archive: ") +
                       c.file);
                ok(!fromPartial.entries.empty(),
                   std::string("the listing is not empty: ") + c.file);
            }

            std::error_code ec;
            fs::remove(partial, ec);
        }
    }

    section("a check from torrent bytes");
    {
        // The search row hands the metadata it already fetched to the check.
        // What is pinned here is the contract when those bytes are missing or
        // are not a torrent: refuse quickly and say why, never fall through to
        // an empty answer.
        std::vector<SecurityFile> entries;
        std::string error;
        ok(!ArchivePreflight::inspectTorrent("", 0, entries, error, nullptr),
           "an empty body is refused");
        ok(!error.empty(), "the empty-body refusal explains itself");

        error.clear();
        ok(!ArchivePreflight::inspectTorrent("this is not a torrent", 0,
                                             entries, error, nullptr),
           "bytes that are not a torrent are refused");
        ok(!error.empty(), "the refusal explains itself");
        ok(entries.empty(), "a refused check lists nothing");
    }

    section("what the tail adds");    {
        // libarchive can stream a ZIP from its local headers alone, so a head
        // never makes things worse; the tail is what recovers the entries the
        // local headers cannot describe. Asserting "at least as many" is the
        // property the preflight actually depends on.
        const fs::path full =
            fs::path(FTORRENT_ARCHIVE_FIXTURES) / "setup.zip";
        const std::string bytes = readFile(full);
        if (!bytes.empty()) {
            const std::size_t window = 65536;
            ArchivePreflight::Snippet headOnly;
            headOnly.fileSize = static_cast<std::int64_t>(bytes.size());
            headOnly.head = bytes.substr(0, std::min(window, bytes.size()));

            ArchivePreflight::Snippet headAndTail = headOnly;
            headAndTail.tail =
                bytes.substr(bytes.size() > window ? bytes.size() - window : 0);

            const fs::path headPath = scratch("head-only.zip");
            const fs::path bothPath = scratch("head-and-tail.zip");
            std::string error;
            ArchivePreflight::writeSparseFile(headPath.string(), headOnly, error);
            ArchivePreflight::writeSparseFile(bothPath.string(), headAndTail, error);

            const ArchiveInspector::ListingAll headListing =
                ArchiveInspector::listEntries(headPath.string());
            const ArchiveInspector::ListingAll bothListing =
                ArchiveInspector::listEntries(bothPath.string());
            ok(!bothListing.ok || !headListing.ok ||
                   bothListing.entries.size() >= headListing.entries.size(),
               "adding the tail never hides entries the head already showed");

            std::error_code ec;
            fs::remove(headPath, ec);
            fs::remove(bothPath, ec);
        }
    }
#else
    std::cout << "\n== archive listing skipped: no libarchive ==\n";
#endif

    std::cout << "\narchive_preflight checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
