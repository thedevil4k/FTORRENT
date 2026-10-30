#include "ArchivePreflight.h"
#include "ArchiveInspector.h"
#include "SystemUtils.h"
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
// viable peers, no port mapping, a small upload cap so a probe never becomes a
// source.
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
    sp.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    sp.set_bool(lt::settings_pack::enable_incoming_utp, false);
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    sp.set_str(lt::settings_pack::user_agent, USER_AGENT);
    sp.set_int(lt::settings_pack::active_limit, 2);
    sp.set_int(lt::settings_pack::alert_queue_size, 50000);
    sp.set_int(lt::settings_pack::upload_rate_limit, 16 * 1024);
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

bool ArchivePreflight::writeSparseFile(const std::string& path,
                                       const Snippet& snippet,
                                       std::string& error) {
    if (snippet.fileSize <= 0) {
        error = "the file is empty";
        return false;
    }
    if (static_cast<std::int64_t>(snippet.head.size()) > snippet.fileSize ||
        static_cast<std::int64_t>(snippet.tail.size()) > snippet.fileSize) {
        error = "the fetched bytes do not fit the file";
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "could not create " + path;
        return false;
    }

    out.write(snippet.head.data(), static_cast<std::streamsize>(snippet.head.size()));

    const std::int64_t tailStart =
        snippet.fileSize - static_cast<std::int64_t>(snippet.tail.size());
    if (!snippet.tail.empty()) {
        out.seekp(tailStart);
        out.write(snippet.tail.data(),
                  static_cast<std::streamsize>(snippet.tail.size()));
    } else {
        // Extremely small files: still make the file the full length.
        out.seekp(snippet.fileSize - 1);
        out.put('\0');
    }
    out.close();
    if (!out) {
        error = "could not write " + path;
        return false;
    }
    return true;
}

std::string ArchivePreflight::tempRoot() {
    return (fs::path(SystemUtils::getConfigDir()) / "preflight").string();
}

bool ArchivePreflight::fetchSnippet(const std::string& magnet,
                                    const std::string& torrentPath,
                                    int fileIndex, Snippet& out,
                                    std::string& error,
                                    const std::atomic<bool>* cancel,
                                    int timeoutSeconds) {
    lt::add_torrent_params atp;
    if (!torrentPath.empty()) {
        try {
            atp = lt::load_torrent_file(torrentPath);
        } catch (const std::exception& e) {
            error = std::string("could not read the .torrent file: ") + e.what();
            return false;
        }
    } else {
        if (magnet.empty()) {
            error = "neither a .torrent file nor a magnet link";
            return false;
        }
        lt::error_code mec;
        atp = lt::parse_magnet_uri(magnet, mec);
        if (mec) {
            error = "bad magnet link";
            return false;
        }
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

    // Every file at zero, so only the deadline pieces below are fetched. The
    // priorities can only be set once the metadata is here, so the torrent is
    // asked to fetch nothing for at most one poll interval before this line.
    std::vector<lt::download_priority_t> priorities(
        static_cast<std::size_t>(storage.num_files()),
        lt::download_priority_t(0));
    handle.prioritize_files(priorities);
    handle.set_piece_deadline(lt::piece_index_t(range.firstPiece), 0,
                              lt::torrent_handle::alert_when_available);
    if (range.lastPiece != range.firstPiece) {
        handle.set_piece_deadline(lt::piece_index_t(range.lastPiece), 0,
                                  lt::torrent_handle::alert_when_available);
    }

    std::map<int, std::string> pieces;
    while (std::chrono::steady_clock::now() < deadline && !cancelled(cancel)) {
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
        if (pieces.count(range.firstPiece) &&
            pieces.count(range.lastPiece)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
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

// Assembles the fetched windows into a sparse file, lists it, and removes
// everything it created. One copy, so both inspect() overloads behave the same.
bool listSnippet(const ArchivePreflight::Snippet& snippet,
                 std::vector<SecurityFile>& entries, std::string& error,
                 const std::atomic<bool>* cancel) {
    std::error_code ec;
    const fs::path workDir =
        fs::path(ArchivePreflight::tempRoot()) /
        ("list-" + std::to_string(static_cast<unsigned long long>(
                      std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::create_directories(workDir, ec);
    if (ec) {
        error = "could not create the temporary folder: " + ec.message();
        return false;
    }
    const fs::path partial = workDir / "head-tail.bin";

    bool ok = ArchivePreflight::writeSparseFile(partial.string(), snippet, error);
    if (ok) {
        const ArchiveInspector::ListingAll listing =
            ArchiveInspector::listEntries(partial.string(), cancel);
        if (!listing.ok) {
            // A truncated index is not a failure of the download: the archive
            // simply needs more than the two ends to be read.
            error = listing.error.empty()
                        ? "the archive index could not be read from its first "
                          "and last piece"
                        : listing.error;
            ok = false;
        } else {
            entries.reserve(listing.entries.size());
            for (const ArchiveInspector::Entry& entry : listing.entries) {
                SecurityFile file;
                file.path = entry.path;
                file.size = static_cast<std::int64_t>(entry.size);
                entries.push_back(std::move(file));
            }
        }
    }

    std::error_code ignored;
    fs::remove_all(workDir, ignored);
    return ok;
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
