#include "ArchivePreflight.h"
#include "ArchiveInspector.h"
#include "ArchiveProbeSession.h"
#include "HttpClient.h"
#include "SystemUtils.h"
#include "TorrentFileList.h"
#include "Version.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace lt = libtorrent;
namespace fs = std::filesystem;

ArchivePreflight::PieceRange
ArchivePreflight::computeRange(std::int64_t fileOffset, std::int64_t fileSize,
                               int pieceLength, std::int64_t torrentSize) {
    PieceRange range;
    if (fileSize <= 0 || pieceLength <= 0 || fileOffset < 0) return range;

    range.firstPiece = static_cast<int>(fileOffset / pieceLength);
    range.firstOffset = static_cast<int>(fileOffset % pieceLength);

    const std::int64_t firstPieceBytes = std::min<std::int64_t>(
        pieceLength, torrentSize - static_cast<std::int64_t>(range.firstPiece) *
                                       pieceLength);
    range.firstLength = static_cast<int>(
        std::min<std::int64_t>(fileSize, firstPieceBytes - range.firstOffset));

    const std::int64_t lastAbsolute = fileOffset + fileSize - 1;
    range.lastPiece = static_cast<int>(lastAbsolute / pieceLength);
    const std::int64_t lastPieceStart =
        static_cast<std::int64_t>(range.lastPiece) * pieceLength;
    range.lastOffset = static_cast<int>(
        std::max<std::int64_t>(0, fileOffset - lastPieceStart));
    range.lastLength = static_cast<int>((fileOffset + fileSize) -
                                        (lastPieceStart + range.lastOffset));
    return range;
}

std::string ArchivePreflight::tempRoot() {
    return (fs::path(SystemUtils::getConfigDir()) / "preflight").string();
}

namespace {

// Builds the torrent parameters from whichever source the row has: a .torrent
// URL, a local file, or a magnet. A downloaded .torrent is checked against the
// magnet's info hash before its bytes are trusted.
bool loadTorrentSource(const std::string& magnet,
                       const std::string& torrentPathOrUrl,
                       lt::add_torrent_params& atp, std::string& error) {
    std::string fileError;
    bool haveFile = false;

    if (torrentPathOrUrl.rfind("https://", 0) == 0) {
        HttpClient::Response res =
            HttpClient::get(torrentPathOrUrl, Version::httpUserAgent(), 15);
        if (!res.ok) {
            fileError = res.error;
        } else if (res.body.empty()) {
            fileError = "the .torrent link returned nothing";
        } else {
            try {
                atp = lt::load_torrent_buffer(
                    lt::span<char const>(res.body.data(), res.body.size()));
                // A mirror serving a different torrent would make the probe
                // read pieces of a file the row does not describe, so the hash
                // has to be the one the magnet names.
                const std::string expected =
                    TorrentFileList::magnetHash(magnet);
                const std::string actual =
                    TorrentFileList::infoHashOfTorrentFile(res.body);
                if (!expected.empty() && !actual.empty() &&
                    expected != actual) {
                    fileError =
                        "the .torrent link points at a different torrent";
                } else {
                    haveFile = true;
                }
            } catch (const std::exception& e) {
                fileError = std::string("could not read the .torrent link: ") +
                            e.what();
            }
        }
    } else if (!torrentPathOrUrl.empty()) {
        try {
            atp = lt::load_torrent_file(torrentPathOrUrl);
            haveFile = true;
        } catch (const std::exception& e) {
            fileError =
                std::string("could not read the .torrent file: ") + e.what();
        }
    }

    if (!haveFile) {
        if (magnet.empty()) {
            error = fileError.empty()
                        ? "neither a .torrent file nor a magnet link"
                        : fileError;
            return false;
        }
        lt::error_code mec;
        atp = lt::parse_magnet_uri(magnet, mec);
        if (mec) {
            error = "bad magnet link";
            return false;
        }
        TorrentFileList::addDefaultTrackers(atp.trackers);
    }
    return true;
}

} // namespace

bool ArchivePreflight::fetchSnippet(const std::string& magnet,
                                    const std::string& torrentPathOrUrl,
                                    int fileIndex, Snippet& out,
                                    std::string& error,
                                    const std::atomic<bool>* cancel,
                                    int timeoutSeconds) {
    lt::add_torrent_params atp;
    if (!loadTorrentSource(magnet, torrentPathOrUrl, atp, error)) return false;
    return fetchSnippet(atp, fileIndex, out, error, cancel, timeoutSeconds);
}

bool ArchivePreflight::fetchSnippet(const lt::add_torrent_params& params,
                                    int fileIndex, Snippet& out,
                                    std::string& error,
                                    const std::atomic<bool>* cancel,
                                    int timeoutSeconds) {
    out = Snippet{};
    error.clear();

    // One session reads the pieces, whichever format the archive is: the first
    // and the last piece are all a ZIP or a RAR5 needs, and a RAR 1.5-4.x walk
    // uses the same session piece by piece.
    ArchiveProbeSession probe(params, fileIndex, cancel, timeoutSeconds, error);
    if (!probe.ok()) return false;

    out.fileSize = probe.fileSize();
    if (!probe.readFirst(out.head, error)) return false;
    return probe.readLast(out.tail, error);
}

namespace {

// libarchive's own wording for "the headers I need are not in these bytes" is
// terse ("Bad RAR file", "Unexpected end of file"). What the user needs to know
// is that this particular archive cannot be listed from its two ends at all,
// which is a property of the file, not a failure of the app.
std::string readableIndexError(const std::string& raw) {
    const std::string lower = [&] {
        std::string out = raw;
        std::transform(
            out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }();
    if (raw.empty()) {
        return "the archive index could not be read from its first and last "
               "piece";
    }
    if (lower.find("bad rar") != std::string::npos ||
        lower.find("rar5") != std::string::npos ||
        lower.find("truncated") != std::string::npos ||
        lower.find("unexpected end") != std::string::npos ||
        lower.find("header") != std::string::npos) {
        return "this archive does not carry its index at its two ends, so it "
               "cannot be listed without downloading it (" +
               raw + ")";
    }
    return raw;
}

// The signature bytes decide which archive this is before libarchive has to say
// something cryptic about it. RAR 1.5-4.x keeps every header interleaved with
// the file data, so a two-piece listing can never see past the first few
// entries: that is a property of the format, not of the check.
const char* OLD_RAR_SIGNATURE = "\x52\x61\x72\x21\x1a\x07\x00";
const std::size_t OLD_RAR_SIGNATURE_SIZE = 7;

bool hasOldRarSignature(const std::string& bytes) {
    return bytes.size() >= OLD_RAR_SIGNATURE_SIZE &&
           bytes.compare(0, OLD_RAR_SIGNATURE_SIZE, OLD_RAR_SIGNATURE,
                         OLD_RAR_SIGNATURE_SIZE) == 0;
}

bool isOldRar(const ArchivePreflight::Snippet& snippet) {
    return hasOldRarSignature(snippet.head);
}

// Copies what an index exposed into the entries the UI shows. The encrypted
// flag is carried through: this is the only point in the app where an archive
// header is actually read before a download.
void copyListing(const ArchiveInspector::ListingAll& listing,
                 std::vector<SecurityFile>& entries) {
    entries.reserve(listing.entries.size());
    for (const ArchiveInspector::Entry& entry : listing.entries) {
        SecurityFile file;
        file.path = entry.path;
        file.size = static_cast<std::int64_t>(entry.size);
        file.encrypted = entry.encrypted;
        entries.push_back(std::move(file));
    }
}

// Lists the index straight from the two windows. Nothing is written anywhere:
// the sparse file this used to build cost a full-size write on NTFS (21 GB of
// zeros for a 21 GB archive), which is where the check used to sit after the
// pieces had already arrived.
bool listSnippet(const ArchivePreflight::Snippet& snippet,
                 std::vector<SecurityFile>& entries, std::string& error,
                 const std::atomic<bool>* cancel,
                 bool* encryptedHeaders = nullptr) {
    if (isOldRar(snippet)) {
        error = "this is a RAR 1.5-4.x archive: it has no central index, so it "
                "needs the bounded header walk";
        return false;
    }

    const ArchiveInspector::ListingAll listing =
        ArchiveInspector::listEntriesFromWindows(snippet.fileSize, snippet.head,
                                                 snippet.tail, cancel);
    if (encryptedHeaders) *encryptedHeaders = listing.encryptedHeaders;
    if (!listing.ok) {
        error = listing.encryptedHeaders
                    ? "the archive headers are encrypted, so its contents "
                      "cannot be listed before downloading it"
                    : readableIndexError(listing.error);
        return false;
    }

    copyListing(listing, entries);
    return true;
}

} // namespace

namespace {

// A RAR 1.5-4.x has no central index, so the walk can be long. It is allowed a
// tenth of the archive's own pieces, capped at 64 (128 MiB with 2 MiB pieces):
// the user gets the first entries and a note instead of a hidden full download.
int oldRarPieceBudget(std::int64_t fileSize, int pieceLength) {
    if (fileSize <= 0 || pieceLength <= 0) return 0;
    const int archivePieces =
        static_cast<int>((fileSize + pieceLength - 1) / pieceLength);
    const int byFraction = std::max(2, archivePieces / 10);
    return std::min(64, byFraction);
}

std::string partialRarNote(const ArchiveInspector::ListingAll& listing,
                           int fetchedPieces, int pieceLength) {
    const long long mib = static_cast<long long>(fetchedPieces) *
                          static_cast<long long>(pieceLength) /
                          (1024LL * 1024LL);
    std::string note =
        "RAR 1.5-4.x has no central index, so this is a partial list: " +
        std::to_string(listing.entries.size()) + " entries read from headers";
    if (mib > 0) note += " (" + std::to_string(mib) + " MiB fetched)";
    note += ". ";
    note += listing.error.empty()
                ? "The header walk stopped early."
                : "The header walk stopped early: " + listing.error + ".";
    return note;
}

} // namespace

bool ArchivePreflight::inspect(const std::string& magnet,
                               const std::string& torrentPath, int fileIndex,
                               std::vector<SecurityFile>& entries,
                               std::string& error,
                               const std::atomic<bool>* cancel,
                               int timeoutSeconds, std::string* note,
                               bool* encryptedHeaders) {
    lt::add_torrent_params atp;
    if (!loadTorrentSource(magnet, torrentPath, atp, error)) return false;
    return inspect(atp, fileIndex, entries, error, cancel, timeoutSeconds, note,
                   encryptedHeaders);
}

bool ArchivePreflight::inspect(const lt::add_torrent_params& params,
                               int fileIndex,
                               std::vector<SecurityFile>& entries,
                               std::string& error,
                               const std::atomic<bool>* cancel,
                               int timeoutSeconds, std::string* note,
                               bool* encryptedHeaders) {
    entries.clear();
    error.clear();
    if (note) note->clear();
    if (encryptedHeaders) *encryptedHeaders = false;

    if (!ArchiveInspector::isAvailable()) {
        error = "archive support is not available in this build";
        return false;
    }

    ArchiveProbeSession probe(params, fileIndex, cancel, timeoutSeconds, error);
    if (!probe.ok()) return false;

    // The first piece also decides the format: RAR 1.5-4.x keeps its headers
    // interleaved with the data, so it gets the header walk below. ZIP and
    // RAR5 still pay only their two ends.
    std::string head;
    if (!probe.readFirst(head, error)) return false;

    if (hasOldRarSignature(head)) {
        probe.setPieceBudget(
            oldRarPieceBudget(probe.fileSize(), probe.pieceLength()));
        const ArchiveInspector::ListingAll listing =
            ArchiveInspector::listEntriesFromWindows(
                probe.fileSize(), head, "", cancel,
                [&probe](std::int64_t offset, std::size_t length,
                         std::string& out, std::string& fetchError) {
                    return probe.readArchiveRange(offset, length, out,
                                                  fetchError);
                });

        if (encryptedHeaders) *encryptedHeaders = listing.encryptedHeaders;

        if (listing.entries.empty()) {
            error =
                listing.encryptedHeaders
                    ? "the archive headers are encrypted, so its contents "
                      "cannot be listed before downloading it"
                    : (listing.ok ? "the archive lists no files"
                                  : (listing.error.empty()
                                         ? "the archive index could not be read"
                                         : listing.error));
            return false;
        }

        copyListing(listing, entries);
        if (!listing.ok && note) {
            *note = partialRarNote(listing, probe.fetchedPieces(),
                                   probe.pieceLength());
        }
        return true;
    }

    std::string tail;
    if (!probe.readLast(tail, error)) return false;

    Snippet snippet;
    snippet.fileSize = probe.fileSize();
    snippet.head = std::move(head);
    snippet.tail = std::move(tail);
    return listSnippet(snippet, entries, error, cancel, encryptedHeaders);
}

bool ArchivePreflight::inspectTorrent(const std::string& torrentBody,
                                      int fileIndex,
                                      std::vector<SecurityFile>& entries,
                                      std::string& error,
                                      const std::atomic<bool>* cancel,
                                      int timeoutSeconds, std::string* note,
                                      bool* encryptedHeaders) {
    entries.clear();
    error.clear();
    if (note) note->clear();
    if (encryptedHeaders) *encryptedHeaders = false;

    if (torrentBody.empty()) {
        error = "no torrent metadata to check";
        return false;
    }

    lt::add_torrent_params atp;
    try {
        atp = lt::load_torrent_buffer(
            lt::span<char const>(torrentBody.data(), torrentBody.size()));
    } catch (const std::exception& e) {
        error = std::string("could not read the torrent metadata: ") + e.what();
        return false;
    }
    if (!atp.ti) {
        error = "the torrent metadata carries no file list";
        return false;
    }
    // The encrypted-header flag travels all the way down: a search row that
    // already holds the .torrent bytes checks the archive through this path,
    // and the lock badge must appear there too.
    return inspect(atp, fileIndex, entries, error, cancel, timeoutSeconds, note,
                   encryptedHeaders);
}
