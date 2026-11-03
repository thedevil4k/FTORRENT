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

// A RAR 1.5-4.x header walk asks for one piece per header region. One piece
// that never arrives stops the walk instead of spending the whole deadline on
// it, so the user still gets the entries read so far.
const int PIECE_WAIT_SECONDS = 20;

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

namespace {

// One throwaway libtorrent session that reads arbitrary ranges of one file.
// It started as the two-piece probe and now also backs the RAR 1.5-4.x header
// walk: libarchive asks for a header, this fetches the piece that holds it and
// skips over the compressed data, so the archive itself is never downloaded.
class ProbeSession {
public:
    ProbeSession(const lt::add_torrent_params& params, int fileIndex,
                 const std::atomic<bool>* cancel, int timeoutSeconds,
                 std::string& error)
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
        m_workDir = fs::path(ArchivePreflight::tempRoot()) /
                    ("run-" + std::to_string(static_cast<unsigned long long>(
                                  std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count())));
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
        atp.flags &= ~(lt::torrent_flags::auto_managed |
                       lt::torrent_flags::paused);

        try {
            const lt::session_params settings(probeSettings());
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
            now + std::chrono::seconds(std::min(timeoutSeconds,
                                                METADATA_TIMEOUT_SECONDS));
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
        // Pieces are wanted on their own through piece priorities, which
        // override the file priorities.
        std::vector<lt::download_priority_t> priorities(
            static_cast<std::size_t>(m_storage->num_files()),
            lt::download_priority_t(0));
        m_handle.prioritize_files(priorities);

        // Endpoints handed to the probe directly are remembered too, so the
        // revival below has something to reconnect to if the swarm never
        // reported a connected peer.
        for (const lt::tcp::endpoint& endpoint : params.peers) {
            rememberPeer(endpoint);
        }

        m_ok = true;
    }

    ~ProbeSession() {
        if (m_ses && m_handle.is_valid()) {
            m_ses->remove_torrent(m_handle, lt::session_handle::delete_files);
        }
        if (m_ses) {
            // The session is torn down here instead of in the member
            // destructor so the torrent is gone first: removing a torrent from
            // a session that is already shutting down blocked the caller.
            m_ses.reset();
        }
        if (!m_workDir.empty()) {
            std::error_code ignored;
            fs::remove_all(m_workDir, ignored);
        }
    }

    bool ok() const { return m_ok; }
    std::int64_t fileSize() const { return m_fileSize; }
    int pieceLength() const { return m_pieceLength; }
    int fetchedPieces() const { return m_fetchedPieces; }

    // At most this many distinct pieces may be fetched. Zero means no limit.
    void setPieceBudget(int pieces) { m_budget = pieces; }

    bool readFirst(std::string& out, std::string& error) {
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

    bool readLast(std::string& out, std::string& error) {
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

    // Reads bytes relative to the start of the archive file, for libarchive.
    bool readArchiveRange(std::int64_t offset, std::size_t length,
                          std::string& out, std::string& error) {
        if (offset < 0 || length == 0 ||
            offset > m_fileSize ||
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

private:
    // Pieces are read forward, so the oldest ones can go: the bytes stay in the
    // temporary storage and are read back from disk if the archive ever asks
    // for them again, which costs no network. The cap keeps a 16 MiB-piece
    // torrent from turning a bounded check into a gigabyte of memory.
    static constexpr std::size_t CACHE_LIMIT_BYTES = 128u * 1024u * 1024u;

    void storePiece(int index, std::string bytes) {
        m_cacheBytes += bytes.size();
        m_cache[index] = std::move(bytes);
        while (m_cacheBytes > CACHE_LIMIT_BYTES && m_cache.size() > 1) {
            auto oldest = m_cache.begin();
            if (oldest->first == index) ++oldest;
            if (oldest == m_cache.end()) break;
            m_cacheBytes -= oldest->second.size();
            m_cache.erase(oldest);
        }
    }

    void collectAlerts() {
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

    // With every file deselected the initial check ends with the torrent
    // "finished": nothing is wanted. A piece priority set before that point is
    // wiped by the check, and the torrent then sits finished, never contacting
    // a peer, until the caller gives up. Measuring it on a local seeder showed
    // exactly that: piece 0 never arrived, read_piece failed, and the check
    // waited out its whole timeout. Waiting for the check to settle and then
    // asking for the piece revives the torrent and downloads only that piece.
    void settleFilePriorities() {
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

    // Every piece asked for is recorded here, whether it is cached, still on
    // disk, or still in flight.
    std::map<int, bool> m_asked;

    // Asks the swarm for one piece. The piece is wanted on its own: every file
    // stays at priority zero, so nothing but the pieces asked for here is ever
    // fetched.
    //
    // For a moment after its initial check libtorrent has no piece layer yet
    // and throws "a piece layer is invalid" (libtorrent:215) at the caller.
    // That is a transient state, not a failure, so the request is retried.
    bool requestPiece(int piece) {
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

    void rememberPeer(const lt::tcp::endpoint& endpoint) {
        if (endpoint.port() == 0) return;
        if (std::find(m_seenPeers.begin(), m_seenPeers.end(), endpoint) !=
            m_seenPeers.end()) {
            return;
        }
        if (m_seenPeers.size() < MAX_SEEN_PEERS) m_seenPeers.push_back(endpoint);
    }

    // With every file deselected libtorrent marks the torrent "finished" as
    // soon as the last wanted piece arrives, and a finished torrent drops its
    // peers and then ignores connect_peer for endpoints it already tried: on a
    // local seeder the first piece arrived in 0.3 s and every later piece never
    // did, with connect_peer called again every half second. Clearing the peer
    // list first makes the same endpoints connect again, and costs nothing else.
    // The peers are remembered while they are still connected, because once the
    // torrent is finished the list is already empty.
    void reviveSwarm(bool force = false) {
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - m_lastRevive < std::chrono::milliseconds(250)) {
            return;
        }
        m_lastRevive = now;

        std::vector<lt::peer_info> connected;
        try {
            m_handle.get_peer_info(connected);
        } catch (const std::exception&) {
            // For a moment right after the check finishes, libtorrent has no
            // piece layer yet and refuses to report peers. There is nothing to
            // do but wait for the next attempt.
            return;
        }
        for (const lt::peer_info& peer : connected) {
            rememberPeer(peer.remote_endpoint());
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

    bool waitForPiece(int piece, std::string& error, int maxWaitSeconds = 0) {
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
            // The first request only needs the check to have settled: the
            // torrent has just started and its peers are still connecting.
            // Later ones come after a piece was already delivered, which is
            // exactly when libtorrent has marked the torrent finished.
            reviveSwarm(true);
            if (!requestPiece(piece)) {
                error = "the torrent would not accept the request for a piece";
                return false;
            }
        }

        bool askedForRead = false;
        // One piece that never arrives must not eat the whole deadline: the
        // walk stops there, says so, and keeps what it already read.
        const auto pieceDeadline =
            maxWaitSeconds > 0
                ? std::min(m_deadline,
                           std::chrono::steady_clock::now() +
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

            // A piece already on disk produces no deadline alert; ask for its
            // bytes directly. This only reads what is there.
            if (!askedForRead && m_handle.have_piece(lt::piece_index_t(piece))) {
                m_handle.read_piece(lt::piece_index_t(piece));
                askedForRead = true;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - m_lastAsk >= std::chrono::seconds(5)) {
                // A deadline set before the torrent finished starting is
                // answered with an error and never retried.
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

    const std::atomic<bool>* m_cancel = nullptr;
    std::unique_ptr<lt::session> m_ses;
    lt::torrent_handle m_handle;
    std::shared_ptr<const lt::torrent_info> m_info;
    const lt::file_storage* m_storage = nullptr;
    fs::path m_workDir;
    ArchivePreflight::PieceRange m_range;
    std::int64_t m_fileOffset = 0;
    std::int64_t m_fileSize = 0;
    int m_pieceLength = 0;
    int m_pieceCount = 0;
    int m_budget = 0;
    int m_fetchedPieces = 0;
    bool m_budgetHit = false;
    bool m_settled = false;
    bool m_ok = false;
    std::map<int, std::string> m_cache;
    std::size_t m_cacheBytes = 0;
    static constexpr std::size_t MAX_SEEN_PEERS = 40;
    std::vector<lt::tcp::endpoint> m_seenPeers;
    std::chrono::steady_clock::time_point m_lastRevive{};
    std::chrono::steady_clock::time_point m_deadline;
    std::chrono::steady_clock::time_point m_lastAsk;
};

} // namespace

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
    ProbeSession probe(params, fileIndex, cancel, timeoutSeconds, error);
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
    note += listing.error.empty() ? "The header walk stopped early."
                                  : "The header walk stopped early: " +
                                        listing.error + ".";
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
    return inspect(atp, fileIndex, entries, error, cancel, timeoutSeconds,
                   note, encryptedHeaders);
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

    ProbeSession probe(params, fileIndex, cancel, timeoutSeconds, error);
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
            error = listing.encryptedHeaders
                        ? "the archive headers are encrypted, so its contents "
                          "cannot be listed before downloading it"
                        : (listing.ok
                               ? "the archive lists no files"
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
    return inspect(atp, fileIndex, entries, error, cancel, timeoutSeconds,
                   note);
}
