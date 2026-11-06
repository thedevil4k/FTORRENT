#include "ArchiveProbeSession.h"

#include "TorrentFileList.h"
#include "Version.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <system_error>
#include <thread>
#include <utility>

namespace lt = libtorrent;
namespace fs = std::filesystem;

namespace {

// A probe is a question, not a download: if the metadata has not arrived by
// then, the answer is "not now" and the torrent is dropped.
const int METADATA_TIMEOUT_SECONDS = 25;

// A RAR 1.5-4.x header walk asks for one piece per header region. One piece
// that never arrives stops the walk instead of spending the whole deadline on
// it, so the user still gets the entries read so far.
const int PIECE_WAIT_SECONDS = 20;

// How long the teardown waits for libtorrent to let go of a torrent that was
// removed. It is a safety net, not the normal path: the wait ends as soon as
// the handle is invalid, which is what libtorrent does once its own I/O tasks
// are done. The cap only stops a stuck disk thread from hanging the caller.
const int REMOVAL_WAIT_SECONDS = 10;

bool cancelled(const std::atomic<bool>* flag) {
    return flag && flag->load();
}

// Same peer-session setup the file-list probe uses: DHT and trackers for
// viable peers, no port mapping, and nothing wanted except the pieces the
// caller asks for.
//
// There is deliberately no upload cap: a probe that already has nothing to
// share was being choked by every peer it met, and the pieces never arrived. It
// still cannot become a source -- it wants a few pieces of a 20 GB file and is
// removed the moment they are in hand -- so what the cap bought was nothing and
// what it cost was the whole check.
//
// The DHT and the local discovery are only needed when the caller does not
// already know a peer. A probe that was handed its peers talks to them
// directly, and joining the DHT for that would be a pointless trip to the
// public routers -- and one more thing that can go wrong on the way (see the
// destructor below).
lt::settings_pack probeSettings(bool discovery) {
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::alert_mask,
               lt::alert_category::error | lt::alert_category::storage |
                   lt::alert_category::status | lt::alert_category::dht |
                   lt::alert_category::tracker);
    sp.set_bool(lt::settings_pack::enable_dht, discovery);
    sp.set_bool(lt::settings_pack::enable_lsd, discovery);
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    // uTP is left on: some swarms only reach us over it, and it is a transport,
    // not an upload.
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    // Same bootstrap as the file-list probe: the default routers do not answer
    // everywhere, and a magnet-only torrent then never meets a peer.
    if (discovery) {
        sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
                   TorrentFileList::dhtBootstrapNodes());
    }
    sp.set_str(lt::settings_pack::user_agent, Version::httpUserAgent());
    sp.set_int(lt::settings_pack::active_limit, 2);
    sp.set_int(lt::settings_pack::alert_queue_size, 50000);
    return sp;
}

} // namespace

void ArchiveProbeSession::shutdown(lt::session& session,
                                   lt::torrent_handle& handle) {
    // The torrent is removed and the removal is waited for. libtorrent's own
    // words: remove_torrent() is non-blocking, and the handle stays valid
    // "until all libtorrent tasks (such as I/O tasks) release their references
    // to the torrent". So the handle going invalid is the moment the disk
    // thread is done with this torrent -- and only after that may the session
    // be destroyed and the files deleted. Skipping the wait is what turns a
    // finished check into an access violation in a background thread.
    if (handle.is_valid()) {
        session.remove_torrent(handle);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(REMOVAL_WAIT_SECONDS);
        while (handle.is_valid() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    handle = lt::torrent_handle();
}

ArchiveProbeSession::ArchiveProbeSession(const lt::add_torrent_params& params,
                                         int fileIndex,
                                         const std::atomic<bool>* cancel,
                                         int timeoutSeconds, std::string& error)
    : m_cancel(cancel) {
    if (fileIndex < 0) {
        error = "no file to look at";
        return;
    }
    if (cancelled(m_cancel)) {
        error = "cancelled";
        return;
    }

    std::error_code ec;
    m_workDir =
        fs::path(ArchivePreflight::tempRoot()) /
        ("run-" +
         std::to_string(static_cast<unsigned long long>(
             std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::create_directories(m_workDir, ec);
    if (ec) {
        error = "could not create the temporary folder: " + ec.message();
        return;
    }

    lt::add_torrent_params atp = params;
    atp.save_path = m_workDir.string();
    if (atp.trackers.empty()) {
        TorrentFileList::addDefaultTrackers(atp.trackers);
    }
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);

    try {
        // A probe that was handed its peers does not need to find any: it
        // talks to them directly, and the swarm's discovery stays out of it.
        const bool discovery = params.peers.empty();
        lt::session_params settings(probeSettings(discovery));
        // The probe reads a handful of pieces and throws the session away.
        // The default disk I/O on Windows is the multi-threaded pread one,
        // whose write cache is not always released before the session goes:
        // the thread was left touching a freed cache and the process died with
        // an access violation. The portable implementation is single-threaded
        // and simple, which is all a probe needs.
        settings.disk_io_constructor = lt::posix_disk_io_constructor;
        m_ses = std::make_unique<lt::session>(settings);
        m_handle = m_ses->add_torrent(atp);
    } catch (const std::exception& e) {
        error = std::string("could not query the swarm: ") + e.what();
        return;
    }
    if (!m_handle.is_valid()) {
        error = "the torrent was rejected";
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    m_deadline = now + std::chrono::seconds(timeoutSeconds);
    const auto metadataDeadline =
        now + std::chrono::seconds(
                  std::min(timeoutSeconds, METADATA_TIMEOUT_SECONDS));
    while (std::chrono::steady_clock::now() < metadataDeadline) {
        if (cancelled(m_cancel)) {
            error = "cancelled";
            return;
        }
        std::vector<lt::alert*> alerts;
        m_ses->pop_alerts(&alerts);
        m_info = m_handle.torrent_file();
        if (m_info && m_info->num_files() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!m_info || m_info->num_files() <= 0) {
        error = "no peers answered with the metadata";
        return;
    }

#if LIBTORRENT_VERSION_NUM >= 20100
    m_storage = &m_info->layout();
#else
    m_storage = &m_info->files();
#endif
    if (fileIndex >= m_storage->num_files()) {
        error = "the file is not in the torrent";
        return;
    }

    const lt::file_index_t index(fileIndex);
    m_fileOffset = m_storage->file_offset(index);
    m_fileSize = m_storage->file_size(index);
    m_pieceLength = m_info->piece_length();
    m_pieceCount = m_info->num_pieces();
    m_range = ArchivePreflight::computeRange(
        m_fileOffset, m_fileSize, m_pieceLength, m_info->total_size());

    // Every file at zero: the check must never pull the file it describes.
    // Pieces are wanted on their own through piece priorities, which override
    // the file priorities.
    std::vector<lt::download_priority_t> priorities(
        static_cast<std::size_t>(m_storage->num_files()),
        lt::download_priority_t(0));
    m_handle.prioritize_files(priorities);

    // Endpoints handed to the probe directly are remembered too, so the revival
    // below has something to reconnect to if the swarm never reported a
    // connected peer.
    for (const lt::tcp::endpoint& endpoint : params.peers) {
        rememberPeer(endpoint);
    }

    m_ok = true;
}

ArchiveProbeSession::~ArchiveProbeSession() {
    // The torrent is removed first, and the removal is waited for. libtorrent
    // keeps the torrent alive until its own I/O tasks are done with it, and a
    // session torn down while that thread is still working on those tasks
    // leaves the thread running on freed memory: the process dies with an
    // access violation in a background thread. Only once the torrent is gone
    // may the session go, and only then may its files be deleted.
    if (m_ses) {
        shutdown(*m_ses, m_handle);
        m_ses.reset();
    }
    if (!m_workDir.empty()) {
        std::error_code ignored;
        fs::remove_all(m_workDir, ignored);
    }
}

bool ArchiveProbeSession::readFirst(std::string& out, std::string& error) {
    if (!waitForPiece(m_range.firstPiece, error)) return false;
    const std::string& piece = m_cache[m_range.firstPiece];
    if (m_range.firstOffset + m_range.firstLength >
        static_cast<int>(piece.size())) {
        error = "the first piece is shorter than expected";
        return false;
    }
    out = piece.substr(static_cast<std::size_t>(m_range.firstOffset),
                       static_cast<std::size_t>(m_range.firstLength));
    return true;
}

bool ArchiveProbeSession::readLast(std::string& out, std::string& error) {
    if (!waitForPiece(m_range.lastPiece, error)) return false;
    const std::string& piece = m_cache[m_range.lastPiece];
    if (m_range.lastOffset + m_range.lastLength >
        static_cast<int>(piece.size())) {
        error = "the last piece is shorter than expected";
        return false;
    }
    out = piece.substr(static_cast<std::size_t>(m_range.lastOffset),
                       static_cast<std::size_t>(m_range.lastLength));
    return true;
}

bool ArchiveProbeSession::readArchiveRange(std::int64_t offset,
                                           std::size_t length, std::string& out,
                                           std::string& error) {
    if (offset < 0 || length == 0 || offset > m_fileSize ||
        static_cast<std::int64_t>(length) > m_fileSize - offset) {
        error = "the archive asked for bytes outside the file";
        return false;
    }
    out.clear();
    out.reserve(length);
    std::size_t done = 0;
    while (done < length) {
        const std::int64_t absolute =
            m_fileOffset + offset + static_cast<std::int64_t>(done);
        const int piece = static_cast<int>(absolute / m_pieceLength);
        const std::size_t inPiece =
            static_cast<std::size_t>(absolute % m_pieceLength);
        if (!waitForPiece(piece, error, PIECE_WAIT_SECONDS)) return false;
        const std::string& bytes = m_cache[piece];
        if (inPiece >= bytes.size()) {
            error = "the fetched piece is shorter than expected";
            return false;
        }
        const std::size_t take =
            std::min(length - done, bytes.size() - inPiece);
        out.append(bytes, inPiece, take);
        done += take;
    }
    return true;
}

void ArchiveProbeSession::storePiece(int index, std::string bytes) {
    // Pieces are read forward, so the oldest ones can go: the bytes stay in the
    // temporary storage and are read back from disk if the archive ever asks
    // for them again, which costs no network. The cap keeps a 16 MiB-piece
    // torrent from turning a bounded check into a gigabyte of memory.
    m_cacheBytes += bytes.size();
    m_cache[index] = std::move(bytes);
    while (m_cacheBytes > CACHE_LIMIT_BYTES && m_cache.size() > 1) {
        auto oldest = m_cache.begin();
        if (oldest->first == index) ++oldest;
        if (oldest == m_cache.end()) break;
        m_cacheBytes -= oldest->second.size();
        m_cache.erase(oldest);
    }

    // The bytes are in hand, so the check stops wanting this piece. Without
    // this, the piece priority survives the arrival and the piece is asked
    // for again: the torrent turns "finished" as soon as the last wanted
    // piece lands, drops its peers, and the revival below reconnects - and
    // libtorrent then serves the same piece from the new peer a second time.
    // On a loopback seeder that showed up as twice the uploaded bytes.
    try {
        m_handle.piece_priority(lt::piece_index_t(index),
                                lt::download_priority_t(0));
    } catch (const std::exception&) {
        // The transient "a piece layer is invalid" state again: the priority
        // stays, and the piece is not fetched twice because the next request
        // goes through the cache.
    }
}

void ArchiveProbeSession::collectAlerts() {
    std::vector<lt::alert*> alerts;
    m_ses->pop_alerts(&alerts);
    for (lt::alert* alert : alerts) {
        if (auto* read = lt::alert_cast<lt::read_piece_alert>(alert)) {
            if (read->error || !read->buffer) continue;
            storePiece(static_cast<int>(read->piece),
                       std::string(read->buffer.get(),
                                   static_cast<std::size_t>(read->size)));
        }
    }
}

void ArchiveProbeSession::settleFilePriorities() {
    // With every file deselected the initial check ends with the torrent
    // "finished": nothing is wanted. A piece priority set before that point is
    // wiped by the check, and the torrent then sits finished, never contacting
    // a peer, until the caller gives up. Measuring it on a local seeder showed
    // exactly that: piece 0 never arrived, read_piece failed, and the check
    // waited out its whole timeout. Waiting for the check to settle and then
    // asking for the piece revives the torrent and downloads only that piece.
    if (m_settled) return;
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until) {
        const lt::torrent_status status = m_handle.status();
        if (status.is_finished ||
            status.state == lt::torrent_status::finished ||
            status.state == lt::torrent_status::seeding) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    m_settled = true;
}

bool ArchiveProbeSession::requestPiece(int piece) {
    // Asks the swarm for one piece. The piece is wanted on its own: every file
    // stays at priority zero, so nothing but the pieces asked for here is ever
    // fetched.
    //
    // For a moment after its initial check libtorrent has no piece layer yet
    // and throws "a piece layer is invalid" (libtorrent:215) at the caller.
    // That is a transient state, not a failure, so the request is retried.
    for (int attempt = 0; attempt < 10; ++attempt) {
        try {
            m_handle.piece_priority(lt::piece_index_t(piece),
                                    lt::download_priority_t(4));
            m_handle.set_piece_deadline(
                lt::piece_index_t(piece), 0,
                lt::torrent_handle::alert_when_available);
        } catch (const std::exception&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        m_asked[piece] = true;
        ++m_fetchedPieces;
        m_lastAsk = std::chrono::steady_clock::now();
        return true;
    }
    return false;
}

void ArchiveProbeSession::rememberPeer(const lt::tcp::endpoint& endpoint) {
    if (endpoint.port() == 0) return;
    if (std::find(m_seenPeers.begin(), m_seenPeers.end(), endpoint) !=
        m_seenPeers.end()) {
        return;
    }
    if (m_seenPeers.size() < MAX_SEEN_PEERS) m_seenPeers.push_back(endpoint);
}

void ArchiveProbeSession::reviveSwarm(bool force) {
    // With every file deselected libtorrent marks the torrent "finished" as
    // soon as the last wanted piece arrives, and a finished torrent drops its
    // peers and then ignores connect_peer for endpoints it already tried: on a
    // local seeder the first piece arrived in 0.3 s and every later piece never
    // did, with connect_peer called again every half second. Clearing the peer
    // list first makes the same endpoints connect again, and costs nothing
    // else. The peers are remembered while they are still connected, because
    // once the torrent is finished the list is already empty.
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - m_lastRevive < std::chrono::milliseconds(250)) {
        return;
    }
    m_lastRevive = now;

    std::vector<lt::peer_info> connected;
    try {
        m_handle.get_peer_info(connected);
    } catch (const std::exception&) {
        // For a moment right after the check finishes, libtorrent has no piece
        // layer yet and refuses to report peers. There is nothing to do but
        // wait for the next attempt.
        return;
    }
    for (const lt::peer_info& peer : connected) {
        // libtorrent 2.1 removed peer_info::ip; 2.0 only has that spelling.
#if LIBTORRENT_VERSION_NUM >= 20100
        rememberPeer(peer.remote_endpoint());
#else
        rememberPeer(peer.ip);
#endif
    }
    if (!connected.empty() || m_seenPeers.empty()) return;

    try {
        m_handle.clear_peers();
        for (const lt::tcp::endpoint& endpoint : m_seenPeers) {
            m_handle.connect_peer(endpoint);
        }
    } catch (const std::exception&) {
        // Same transient state as above: the next attempt tries again.
    }
}

bool ArchiveProbeSession::waitForPiece(int piece, std::string& error,
                                       int maxWaitSeconds) {
    if (m_cache.count(piece)) return true;
    if (cancelled(m_cancel)) {
        error = "cancelled";
        return false;
    }
    if (piece < 0 || piece >= m_pieceCount) {
        error = "the archive points outside the torrent";
        return false;
    }

    const bool alreadyAsked = m_asked.count(piece) != 0;
    if (!alreadyAsked) {
        settleFilePriorities();
        if (m_budget > 0 && m_fetchedPieces >= m_budget) {
            m_budgetHit = true;
            error = "the archive header walk reached its piece budget";
            return false;
        }
        // The first request only needs the check to have settled: the torrent
        // has just started and its peers are still connecting. Later ones come
        // after a piece was already delivered, which is exactly when libtorrent
        // has marked the torrent finished.
        reviveSwarm(true);
        if (!requestPiece(piece)) {
            error = "the torrent would not accept the request for a piece";
            return false;
        }
    }

    bool askedForRead = false;
    // One piece that never arrives must not eat the whole deadline: the walk
    // stops there, says so, and keeps what it already read.
    const auto pieceDeadline =
        maxWaitSeconds > 0
            ? std::min(m_deadline, std::chrono::steady_clock::now() +
                                       std::chrono::seconds(maxWaitSeconds))
            : m_deadline;
    while (std::chrono::steady_clock::now() < pieceDeadline) {
        if (cancelled(m_cancel)) {
            error = "cancelled";
            return false;
        }
        collectAlerts();
        if (m_cache.count(piece)) return true;
        reviveSwarm();

        // A piece already on disk produces no deadline alert; ask for its bytes
        // directly. This only reads what is there.
        if (!askedForRead && m_handle.have_piece(lt::piece_index_t(piece))) {
            m_handle.read_piece(lt::piece_index_t(piece));
            askedForRead = true;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastAsk >= std::chrono::seconds(5)) {
            // A deadline set before the torrent finished starting is answered
            // with an error and never retried.
            m_handle.set_piece_deadline(
                lt::piece_index_t(piece), 0,
                lt::torrent_handle::alert_when_available);
            m_lastAsk = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    if (cancelled(m_cancel)) {
        error = "cancelled";
    } else if (m_budgetHit) {
        error = "the archive header walk reached its piece budget";
    } else {
        error = "the archive headers did not arrive in time";
    }
    return false;
}
