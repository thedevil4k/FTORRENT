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

#include <algorithm>
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

#ifdef FTORRENT_HAVE_LIBARCHIVE
    section("listing from two windows, with no file on disk");
    {
        // What the pre-download check does: the index is read straight from
        // the first and last bytes, so a 21 GB archive costs two pieces of
        // memory and nothing else. On NTFS the old sparse file cost a
        // full-length write, which is what made the check hang after the
        // pieces had already arrived.
        const char* fixtures[] = {
            "setup.zip",
            "test_read_format_rar5_stored.rar",
            "test_read_format_rar5_compressed.rar",
        };
        const std::size_t window = 65536;
        for (const char* name : fixtures) {
            const fs::path full = fs::path(FTORRENT_ARCHIVE_FIXTURES) / name;
            const std::string bytes = readFile(full);
            ok(!bytes.empty(), std::string("fixture is present: ") + name);
            if (bytes.empty()) continue;

            const ArchiveInspector::ListingAll fromFull =
                ArchiveInspector::listEntries(full.string());
            const ArchiveInspector::ListingAll fromWindows =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(bytes.size()),
                    bytes.substr(0, std::min(window, bytes.size())),
                    bytes.substr(bytes.size() > window ? bytes.size() - window
                                                      : 0));
            ok(fromFull.ok, std::string("the whole file lists: ") + name);
            ok(fromWindows.ok == fromFull.ok,
               std::string("the two windows list it too: ") + name);
            if (fromWindows.ok && fromFull.ok) {
                ok(namesOf(fromWindows.entries) == namesOf(fromFull.entries),
                   std::string("and see the same entries: ") + name);
                ok(!fromWindows.entries.empty(),
                   std::string("the listing is not empty: ") + name);
            }
        }

        ok(!ArchiveInspector::listEntriesFromWindows(0, "", "").ok,
           "an empty file is refused");
        ok(!ArchiveInspector::listEntriesFromWindows(
                   100, std::string(200, 'x'), std::string()).ok,
           "windows larger than the file are refused");
        ok(!ArchiveInspector::listEntriesFromWindows(16, "not an archive",
                                                     "").ok,
           "bytes that are not an archive are refused");
    }

    section("lazy header walk for RAR 1.5-4.x");
    {
        // A RAR 1.5-4.x keeps no central index. libarchive can still list it if
        // the stream is allowed to fetch the next header on demand: it skips
        // the packed data and asks only for the bytes around a header. The
        // fetcher below serves a local file so the test measures how much of it
        // is actually touched, with no network involved.
        const char* fixtures[] = {
            "test_read_format_rar_filter.rar",
            "test_read_format_rar_unicode.rar",
            "test_read_format_rar_binary_data.rar",
        };
        for (const char* name : fixtures) {
            const fs::path full = fs::path(FTORRENT_ARCHIVE_FIXTURES) / name;
            const std::string bytes = readFile(full);
            ok(!bytes.empty(), std::string("fixture is present: ") + name);
            if (bytes.empty()) continue;

            const ArchiveInspector::ListingAll fromFull =
                ArchiveInspector::listEntries(full.string());
            std::vector<char> touched(bytes.size(), 0);
            std::size_t fetched = 0;
            const ArchiveInspector::ListingAll lazy =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(bytes.size()), "", "", nullptr,
                    [&](std::int64_t offset, std::size_t length,
                        std::string& out, std::string& error) {
                        if (offset < 0 ||
                            static_cast<std::size_t>(offset) + length >
                                bytes.size()) {
                            error = "the test fetcher was asked outside the file";
                            return false;
                        }
                        out.assign(bytes, static_cast<std::size_t>(offset),
                                   length);
                        for (std::size_t i = 0; i < length; ++i) {
                            touched[static_cast<std::size_t>(offset) + i] = 1;
                        }
                        fetched += length;
                        return true;
                    });

            ok(fromFull.ok, std::string("the whole file lists: ") + name);
            ok(lazy.ok, std::string("the lazy walk lists it too: ") + name);
            if (fromFull.ok && lazy.ok) {
                ok(namesOf(lazy.entries) == namesOf(fromFull.entries),
                   std::string("and sees the same entries: ") + name);
                ok(!lazy.entries.empty(),
                   std::string("the lazy listing is not empty: ") + name);
            }

            const std::size_t unique =
                static_cast<std::size_t>(std::count(touched.begin(),
                                                    touched.end(), 1));
            ok(fetched > 0 && unique > 0,
               std::string("the walk asked for header bytes: ") + name);
            if (std::string(name) == "test_read_format_rar_filter.rar") {
                ok(unique < bytes.size(),
                   "the walk does not need every byte of the RAR4");
            }
            if (std::string(name) == "test_read_format_rar_binary_data.rar") {
                // The 1 MB payload is skipped; only the headers and libarchive's
                // read-ahead are touched.
                ok(unique < bytes.size() / 2,
                   "the walk skips most of the compressed data");
            }
        }

        // A fetcher that stops must stop the listing and keep what was already
        // read, because that is exactly what the piece budget or a timeout does
        // in the real check.
        const fs::path binary = fs::path(FTORRENT_ARCHIVE_FIXTURES) /
                                "test_read_format_rar_binary_data.rar";
        const std::string bytes = readFile(binary);
        if (!bytes.empty()) {
            bool firstCall = true;
            const ArchiveInspector::ListingAll stopped =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(bytes.size()), "", "", nullptr,
                    [&](std::int64_t offset, std::size_t length,
                        std::string& out, std::string& error) {
                        if (firstCall) {
                            firstCall = false;
                            out.assign(bytes,
                                       static_cast<std::size_t>(offset),
                                       length);
                            return true;
                        }
                        error = "the test stopped the walk";
                        return false;
                    });
            ok(!stopped.ok, "a failed fetch stops the listing");
            ok(stopped.error.find("test stopped the walk") != std::string::npos,
               "the fetch error is reported, not libarchive's generic one");
        }

        // Encrypted headers: nothing can be listed, and the reader must say so
        // in a way the lock badge can use instead of reporting a broken file.
        const fs::path encrypted =
            fs::path(FTORRENT_ARCHIVE_FIXTURES) /
            "test_read_format_rar_encryption_header.rar";
        const std::string encryptedBytes = readFile(encrypted);
        ok(!encryptedBytes.empty(), "the encrypted-header fixture is present");
        if (!encryptedBytes.empty()) {
            const ArchiveInspector::ListingAll listing =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(encryptedBytes.size()),
                    encryptedBytes, "", nullptr);
            ok(!listing.ok, "an archive with encrypted headers is not listed");
            ok(listing.entries.empty(),
               "and it claims no entries it could not read");
            ok(listing.encryptedHeaders,
               "it is reported as password-protected, not as broken");
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

    section("what the tail adds");
    {
        // libarchive can stream a ZIP from its local headers alone, so a head
        // never makes things worse; the tail is what recovers the entries the
        // local headers cannot describe. Asserting "at least as many" is the
        // property the preflight actually depends on.
        const fs::path full =
            fs::path(FTORRENT_ARCHIVE_FIXTURES) / "setup.zip";
        const std::string bytes = readFile(full);
        if (!bytes.empty()) {
            const std::size_t window = 65536;
            const ArchiveInspector::ListingAll headListing =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(bytes.size()),
                    bytes.substr(0, std::min(window, bytes.size())),
                    std::string());
            const ArchiveInspector::ListingAll bothListing =
                ArchiveInspector::listEntriesFromWindows(
                    static_cast<std::int64_t>(bytes.size()),
                    bytes.substr(0, std::min(window, bytes.size())),
                    bytes.substr(bytes.size() > window ? bytes.size() - window
                                                      : 0));
            ok(!bothListing.ok || !headListing.ok ||
                   bothListing.entries.size() >= headListing.entries.size(),
               "adding the tail never hides entries the head already showed");
        }
    }
#else
    std::cout << "\n== archive listing skipped: no libarchive ==\n";
#endif

    std::cout << "\narchive_preflight checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
