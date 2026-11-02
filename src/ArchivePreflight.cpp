#include "ArchivePreflight.h"
#include "ArchiveInspector.h"
#include "HttpClient.h"
#include "SystemUtils.h"
#include "TorrentFileList.h"
#include "Version.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace lt = libtorrent;
namespace fs = std::filesystem;

namespace {

const std::string USER_AGENT = Version::httpUserAgent();

// A preflight is a question, not a download: if the two pieces have not
// arrived by then, the answer is "not now" and the torrent is dropped.
const int METADATA_TIMEOUT_SECONDS = 25;

bool cancelled(const std::atomic<bool>* flag) {
    return flag && flag->load();
}

// Same peer-session setup the file-list probe uses: DHT and trackers for
// viable peers, no port mapping, and nothing wanted except the two pieces the
// caller asks for.
//
// There is deliberately no upload cap: a probe that already has nothing to
// share was being choked by every peer it met, and the two pieces never
// arrived. It still cannot become a source -- it wants two pieces of a 20 GB
// file and is removed the moment they are in hand -- so what the cap bought
// was nothing and what it cost was the whole check.
lt::settings_pack probeSettings() {
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::alert_mask,
               lt::alert_category::error | lt::alert_category::storage |
                   lt::alert_category::status | lt::alert_category::dht |
                   lt::alert_category::tracker);
    sp.set_bool(lt::settings_pack::enable_dht, true);
    sp.set_bool(lt::settings_pack::enable_lsd, true);
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    // uTP is left on: some swarms only reach us over it, and it is a transport,
    // not an upload.
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    // Same bootstrap as the file-list probe: the default routers do not answer
    // everywhere, and a magnet-only torrent then never meets a peer.
    sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
               TorrentFileList::dhtBootstrapNodes());
    sp.set_str(lt::settings_pack::user_agent, USER_AGENT);
    sp.set_int(lt::settings_pack::active_limit, 2);
    sp.set_int(lt::settings_pack::alert_queue_size, 50000);
    return sp;
}

} // namespace

ArchivePreflight::PieceRange ArchivePreflight::computeRange(
    std::int64_t fileOffset, std::int64_t fileSize, int pieceLength,
    std::int64_t torrentSize) {
    PieceRange range;
    if (fileSize <= 0 || pieceLength <= 0 || fileOffset < 0) return range;

    range.firstPiece = static_cast<int>(fileOffset / pieceLength);
    range.firstOffset = static_cast<int>(fileOffset % pieceLength);

    const std::int64_t firstPieceBytes = std::min<std::int64_t>(
        pieceLength,
        torrentSize - static_cast<std::int64_t>(range.firstPiece) * pieceLength);
    range.firstLength = static_cast<int>(
        std::min<std::int64_t>(fileSize, firstPieceBytes - range.firstOffset));

    const std::int64_t lastAbsolute = fileOffset + fileSize - 1;
    range.lastPiece = static_cast<int>(lastAbsolute / pieceLength);
    const std::int64_t lastPieceStart =
        static_cast<std::int64_t>(range.lastPiece) * pieceLength;
    range.lastOffset = static_cast<int>(std::max<std::int64_t>(
        0, fileOffset - lastPieceStart));
    range.lastLength =
        static_cast<int>((fileOffset + fileSize) -
                         (lastPieceStart + range.lastOffset));
    return range;
}

std::string ArchivePreflight::tempRoot() {
    return (fs::path(SystemUtils::getConfigDir()) / "preflight").string();
}

bool ArchivePreflight::fetchSnippet(const std::string& magnet,
                                    const std::string& torrentPathOrUrl,
                                    int fileIndex, Snippet& out,
                                    std::string& error,
                                    const std::atomic<bool>* cancel,
                                    int timeoutSeconds) {
    lt::add_torrent_params atp;
    std::string fileError;
    bool haveFile = false;

    // A row can now offer its .torrent link, and that is the cheap path: no
    // metadata exchange with the swarm at all.
    if (torrentPathOrUrl.rfind("https://", 0) == 0) {
        HttpClient::Response res =
            HttpClient::get(torrentPathOrUrl, USER_AGENT, 15);
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
                const std::string expected = TorrentFileList::magnetHash(magnet);
                const std::string actual =
                    TorrentFileList::infoHashOfTorrentFile(res.body);
                if (!expected.empty() && !actual.empty() && expected != actual) {
                    fileError = "the .torrent link points at a different torrent";
                } else {
                    haveFile = true;
                }
            } catch (const std::exception& e) {
                fileError = std::string("could not read the .torrent link: ") + e.what();
            }
        }
    } else if (!torrentPathOrUrl.empty()) {
        try {
            atp = lt::load_torrent_file(torrentPathOrUrl);
            haveFile = true;
        } catch (const std::exception& e) {
            fileError = std::string("could not read the .torrent file: ") + e.what();
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
    return fetchSnippet(atp, fileIndex, out, error, cancel, timeoutSeconds);
}

bool ArchivePreflight::fetchSnippet(const lt::add_torrent_params& params,
                                    int fileIndex, Snippet& out,
                                    std::string& error,
                                    const std::atomic<bool>* cancel,
                                    int timeoutSeconds) {
    out = Snippet{};
    if (fileIndex < 0) {
        error = "no file to look at";
        return false;
    }
    if (cancelled(cancel)) {
        error = "cancelled";
        return false;
    }

    // The pieces go into a directory of their own, and it is deleted below.
    std::error_code ec;
    const fs::path workDir =
        fs::path(tempRoot()) /
        ("run-" + std::to_string(static_cast<unsigned long long>(
                      std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::create_directories(workDir, ec);
    if (ec) {
        error = "could not create the temporary folder: " + ec.message();
        return false;
    }

    auto cleanup = [&]() {
        std::error_code ignored;
        fs::remove_all(workDir, ignored);
    };

    lt::add_torrent_params atp = params;
    // The probe owns where the pieces go; whatever the caller had is replaced.
    atp.save_path = workDir.string();
    // A .torrent rebuilt from metadata carries no announce list, and a torrent
    // with none can only find peers over DHT. The public trackers are added the
    // same way the magnet path adds them, so a check does not depend on the DHT
    // alone.
    if (atp.trackers.empty()) TorrentFileList::addDefaultTrackers(atp.trackers);
    // Nothing but the two pieces below is wanted: every file starts at zero
    // priority, so no other piece is ever requested.
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);

    const lt::session_params sessionSettings(probeSettings());
    lt::session ses(sessionSettings);
    lt::torrent_handle handle;
    try {
        handle = ses.add_torrent(atp);
    } catch (const std::exception& e) {
        error = std::string("could not query the swarm: ") + e.what();
        cleanup();
        return false;
    }
    if (!handle.is_valid()) {
        error = "the torrent was rejected";
        cleanup();
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeoutSeconds);
    std::shared_ptr<const lt::torrent_info> info;
    const auto metadataDeadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(
                                      std::min(timeoutSeconds,
                                               METADATA_TIMEOUT_SECONDS));
    while (std::chrono::steady_clock::now() < metadataDeadline) {
        if (cancelled(cancel)) {
            error = "cancelled";
            break;
        }
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        info = handle.torrent_file();
        if (info && info->num_files() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!info || info->num_files() <= 0) {
        if (error.empty()) error = "no peers answered with the metadata";
        ses.remove_torrent(handle, lt::session_handle::delete_files);
        cleanup();
        return false;
    }

#if LIBTORRENT_VERSION_NUM >= 20100
    const lt::file_storage& storage = info->layout();
#else
    const lt::file_storage& storage = info->files();
#endif
    if (fileIndex >= storage.num_files()) {
        error = "the file is not in the torrent";
        ses.remove_torrent(handle, lt::session_handle::delete_files);
        cleanup();
        return false;
    }

    const lt::file_index_t index(fileIndex);
    const std::int64_t fileOffset = storage.file_offset(index);
    const std::int64_t fileSize = storage.file_size(index);
    const int pieceLength = info->piece_length();
    out.fileSize = fileSize;

    const PieceRange range =
        computeRange(fileOffset, fileSize, pieceLength, info->total_size());

    // Every file at zero: the check must never pull the file it is describing.
    // The two pieces are then wanted on their own, through piece priorities,
    // which override the file priorities. Asking for a deadline alone does
    // nothing when every file is deselected: libtorrent rejected the request
    // ("invalid piece index in slot list") and the check waited out its timeout
    // on every torrent. Measured on a 20 GB RAR with 10 seeds: piece priorities
    // delivered both pieces and pulled 3.9 MiB in total.
    std::vector<lt::download_priority_t> priorities(
        static_cast<std::size_t>(storage.num_files()),
        lt::download_priority_t(0));
    handle.prioritize_files(priorities);
    const lt::download_priority_t wanted(4);   // libtorrent's "normal"
    handle.piece_priority(lt::piece_index_t(range.firstPiece), wanted);
    if (range.lastPiece != range.firstPiece) {
        handle.piece_priority(lt::piece_index_t(range.lastPiece), wanted);
    }

    std::map<int, std::string> pieces;
    auto collectAlerts = [&]() {
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        for (lt::alert* alert : alerts) {
            if (auto* read = lt::alert_cast<lt::read_piece_alert>(alert)) {
                if (read->error || !read->buffer) continue;
                pieces[static_cast<int>(read->piece)] =
                    std::string(read->buffer.get(),
                                static_cast<std::size_t>(read->size));
            }
        }
    };
    auto havePiece = [&](int piece) {
        return pieces.count(piece) != 0 ||
               handle.have_piece(lt::piece_index_t(piece));
    };

    // The deadline is the only thing asked for here, and it is asked again
    // every few seconds: a deadline set before the torrent has finished
    // starting is answered at once with an error and never retried, which is
    // also why the very first request often does nothing.
    bool asked = false;
    auto lastAsk = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !cancelled(cancel)) {
        collectAlerts();
        if (havePiece(range.firstPiece) && havePiece(range.lastPiece)) break;

        const auto now = std::chrono::steady_clock::now();
        if (!asked || now - lastAsk >= std::chrono::seconds(5)) {
            if (!havePiece(range.firstPiece)) {
                handle.set_piece_deadline(
                    lt::piece_index_t(range.firstPiece), 0,
                    lt::torrent_handle::alert_when_available);
            }
            if (range.lastPiece != range.firstPiece &&
                !havePiece(range.lastPiece)) {
                handle.set_piece_deadline(
                    lt::piece_index_t(range.lastPiece), 0,
                    lt::torrent_handle::alert_when_available);
            }
            asked = true;
            lastAsk = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // A piece that is already on disk no longer produces a deadline alert, so
    // the bytes are asked for directly. This only reads what is there: the
    // download itself was driven by the deadline above.
    for (int piece : {range.firstPiece, range.lastPiece}) {
        if (pieces.count(piece) || !handle.have_piece(lt::piece_index_t(piece))) {
            continue;
        }
        handle.read_piece(lt::piece_index_t(piece));
        const auto readUntil = std::chrono::steady_clock::now() +
                               std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < readUntil &&
               !pieces.count(piece)) {
            collectAlerts();
            if (pieces.count(piece)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
    }

    bool ok = true;
    if (cancelled(cancel)) {
        error = "cancelled";
        ok = false;
    } else if (!pieces.count(range.firstPiece) ||
               !pieces.count(range.lastPiece)) {
        error = "the first and last pieces did not arrive in time";
        ok = false;
    }

    if (ok) {
        const std::string& first = pieces[range.firstPiece];
        if (range.firstOffset + range.firstLength >
            static_cast<int>(first.size())) {
            error = "the first piece is shorter than expected";
            ok = false;
        } else {
            out.head = first.substr(static_cast<std::size_t>(range.firstOffset),
                                    static_cast<std::size_t>(range.firstLength));
        }
    }
    if (ok) {
        const std::string& last = pieces[range.lastPiece];
        if (range.lastOffset + range.lastLength >
            static_cast<int>(last.size())) {
            error = "the last piece is shorter than expected";
            ok = false;
        } else {
            out.tail = last.substr(static_cast<std::size_t>(range.lastOffset),
                                   static_cast<std::size_t>(range.lastLength));
        }
    }

    ses.remove_torrent(handle, lt::session_handle::delete_files);
    cleanup();
    return ok;
}

namespace {

// libarchive's own wording for "the headers I need are not in these bytes" is
// terse ("Bad RAR file", "Unexpected end of file"). What the user needs to know
// is that this particular archive cannot be listed from its two ends at all,
// which is a property of the file, not a failure of the app.
std::string readableIndexError(const std::string& raw) {
    const std::string lower = [&] {
        std::string out = raw;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
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

bool isOldRar(const ArchivePreflight::Snippet& snippet) {
    if (snippet.head.size() < OLD_RAR_SIGNATURE_SIZE) return false;
    return snippet.head.compare(0, OLD_RAR_SIGNATURE_SIZE, OLD_RAR_SIGNATURE,
                                OLD_RAR_SIGNATURE_SIZE) == 0;
}

// Lists the index straight from the two windows. Nothing is written anywhere:
// the sparse file this used to build cost a full-size write on NTFS (21 GB of
// zeros for a 21 GB archive), which is where the check used to sit after the
// pieces had already arrived.
bool listSnippet(const ArchivePreflight::Snippet& snippet,
                 std::vector<SecurityFile>& entries, std::string& error,
                 const std::atomic<bool>* cancel) {
    if (isOldRar(snippet)) {
        error = "this is a RAR 1.5-4.x archive: it keeps its headers spread "
                "through the file, so it cannot be listed from its first and "
                "last piece";
        return false;
    }

    const ArchiveInspector::ListingAll listing =
        ArchiveInspector::listEntriesFromWindows(snippet.fileSize, snippet.head,
                                                 snippet.tail, cancel);
    if (!listing.ok) {
        error = readableIndexError(listing.error);
        return false;
    }

    entries.reserve(listing.entries.size());
    for (const ArchiveInspector::Entry& entry : listing.entries) {
        SecurityFile file;
        file.path = entry.path;
        file.size = static_cast<std::int64_t>(entry.size);
        // Carried through so the row can say the archive is
        // password-protected: this is the only point in the app where an
        // archive header is actually read before a download.
        file.encrypted = entry.encrypted;
        entries.push_back(std::move(file));
    }
    return true;
}

} // namespace

bool ArchivePreflight::inspect(const std::string& magnet,
                               const std::string& torrentPath, int fileIndex,
                               std::vector<SecurityFile>& entries,
                               std::string& error,
                               const std::atomic<bool>* cancel,
                               int timeoutSeconds) {
    entries.clear();
    error.clear();

    if (!ArchiveInspector::isAvailable()) {
        error = "archive support is not available in this build";
        return false;
    }

    Snippet snippet;
    if (!fetchSnippet(magnet, torrentPath, fileIndex, snippet, error, cancel,
                      timeoutSeconds)) {
        return false;
    }
    return listSnippet(snippet, entries, error, cancel);
}

bool ArchivePreflight::inspect(const lt::add_torrent_params& params,
                               int fileIndex,
                               std::vector<SecurityFile>& entries,
                               std::string& error,
                               const std::atomic<bool>* cancel,
                               int timeoutSeconds) {
    entries.clear();
    error.clear();

    if (!ArchiveInspector::isAvailable()) {
        error = "archive support is not available in this build";
        return false;
    }

    Snippet snippet;
    if (!fetchSnippet(params, fileIndex, snippet, error, cancel,
                      timeoutSeconds)) {
        return false;
    }
    return listSnippet(snippet, entries, error, cancel);
}

bool ArchivePreflight::inspectTorrent(const std::string& torrentBody,
                                      int fileIndex,
                                      std::vector<SecurityFile>& entries,
                                      std::string& error,
                                      const std::atomic<bool>* cancel,
                                      int timeoutSeconds) {
    entries.clear();
    error.clear();

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
    return inspect(atp, fileIndex, entries, error, cancel, timeoutSeconds);
}
