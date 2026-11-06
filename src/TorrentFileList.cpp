#include "TorrentFileList.h"
#include "HttpClient.h"
#include "PathUtils.h"
#include "SecurityHashes.h"
#include "Version.h"

#include <libtorrent/version.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/write_resume_data.hpp> // write_torrent_file_buf

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <thread>

namespace lt = libtorrent;

namespace {

const std::string USER_AGENT = Version::httpUserAgent();

// .torrent files are metadata; a few hundred kB at most. Anything wildly
// bigger is a captive-portal login page or an error page, not a torrent.
const size_t MAX_TORRENT_BYTES = 8 * 1024 * 1024;

// Lowercase hex of a v1 info hash, the spelling the rest of the app uses for
// cache keys and for comparing a magnet with a .torrent file.
std::string hexOfV1(const lt::sha1_hash& hash) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (unsigned char byte : hash) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

// Turns what the probe saw into one line the user can act on. The old message
// said the same thing whether the swarm was empty, the trackers refused or the
// DHT never started, which made a fixable problem look like a dead torrent.
std::string describeSwarmFailure(int trackerReplies, int trackerPeers,
                                 int trackerErrors, bool dhtBootstrapped) {
    std::string detail;
    if (trackerReplies > 0) {
        detail = "trackers answered " + std::to_string(trackerReplies) +
                 (trackerReplies == 1 ? " time" : " times") + " with " +
                 std::to_string(trackerPeers) +
                 (trackerPeers == 1 ? " peer" : " peers");
    } else if (trackerErrors > 0) {
        detail = "every tracker failed";
    } else {
        detail = "no tracker answered";
    }
    detail += dhtBootstrapped ? ", DHT bootstrapped" : ", DHT did not start";
    return "No peer sent the metadata (" + detail + ")";
}

// Where the throwaway leech session puts the little it downloads. It never
// grows past a few pieces because the torrent is removed the moment the
// metadata arrives.
std::string scratchPath() {
    std::error_code ec;
    std::string dir = PathUtils::getHomePath() + "/.ftorrent_filelist";
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Fills 'out' from an already-parsed torrent_info.
void collectFiles(const lt::torrent_info& ti,
                  std::vector<TorrentFileEntry>& out) {
#if LIBTORRENT_VERSION_NUM >= 20100
    // torrent_info::files() was removed in libtorrent 2.1, use layout()
    // instead.
    const lt::file_storage& fs = ti.layout();
#else
    const lt::file_storage& fs = ti.files();
#endif
    out.clear();
    out.reserve(static_cast<size_t>(std::max(1, fs.num_files())));
    for (int i = 0; i < fs.num_files(); ++i) {
        // v2 and hybrid torrents carry internal pad files to align pieces.
        // They are created on disk but are not part of the content, so
        // showing them here would only make the list longer and scarier.
        if (fs.pad_file_at(lt::file_index_t(i))) continue;
        TorrentFileEntry e;
        e.fileIndex = i;
        // file_path(), not file_name(): since libtorrent 2.x file_name() hands
        // back only the last component, which would hide that a file sits in
        // some folder and, worse, that two files of the same name are
        // different.
        e.path = fs.file_path(lt::file_index_t(i));
        e.size = fs.file_size(lt::file_index_t(i));
        const auto flags = fs.file_flags(lt::file_index_t(i));
        e.hidden = static_cast<bool>(flags & lt::file_storage::flag_hidden);
        e.executable =
            static_cast<bool>(flags & lt::file_storage::flag_executable);
        e.symlink = static_cast<bool>(flags & lt::file_storage::flag_symlink);
        e.absolutePath = fs.file_absolute_path(lt::file_index_t(i));
        if (e.symlink) {
            e.symlinkPath = fs.symlink(lt::file_index_t(i));
        }
        // Only a v2/hybrid file that fits in one 16 KiB block carries a hash
        // that is really the file's SHA-256; see SecurityHashes.
        SecurityHashes::fillWholeFileHash(ti, fs, i, e);
        out.push_back(std::move(e));
    }
}

bool resolveFromTorrentUrl(const std::string& url,
                           std::vector<TorrentFileEntry>& out,
                           std::string& error, std::string* infoHashHex,
                           std::string* torrentBody) {
    HttpClient::Response res = HttpClient::get(url, USER_AGENT, 15);
    if (!res.ok) {
        error = res.error;
        return false;
    }
    if (res.body.empty() || res.body.size() > MAX_TORRENT_BYTES) {
        error = "Not a .torrent file";
        return false;
    }
    std::string parseError;
    if (!TorrentFileList::parseTorrentFile(res.body, out, parseError,
                                           infoHashHex)) {
        error = parseError;
        return false;
    }
    if (torrentBody) *torrentBody = res.body;
    return true;
}

bool resolveFromMagnet(const std::string& magnet,
                       std::vector<TorrentFileEntry>& out, std::string& error,
                       int timeoutSeconds, std::string* torrentBody) {
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(magnet, ec);
    if (ec) {
        error = "Bad magnet link";
        return false;
    }
    if (!atp.info_hashes.has_v1() && !atp.info_hashes.has_v2()) {
        error = "Magnet link carries no info hash";
        return false;
    }

    // The magnet's own trackers first; the public ones fill in what it lacks.
    // A trackerless magnet (apibay publishes them that way) has no other way
    // to meet a peer on a network where the default DHT routers are blocked.
    TorrentFileList::addDefaultTrackers(atp.trackers);

    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::alert_mask,
               lt::alert_category::error | lt::alert_category::dht |
                   lt::alert_category::tracker | lt::alert_category::status);
    // The metadata only ever comes from peers, and peers reach us over UDP
    // (DHT) or are handed to us by trackers, so both must be on.
    sp.set_bool(lt::settings_pack::enable_dht, true);
    sp.set_bool(lt::settings_pack::enable_lsd, true);
    // Port mapping cannot be relied on inside a sandbox or behind CGNAT and
    // only slows the probe down.
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    sp.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    sp.set_bool(lt::settings_pack::enable_incoming_utp, false);
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    // A fresh session knows nobody. Without explicit nodes it relies on
    // libtorrent's built-in routers, which do not answer from every network,
    // and then DHT-only magnets never find a peer.
    sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
               TorrentFileList::dhtBootstrapNodes());
    sp.set_str(lt::settings_pack::user_agent, USER_AGENT);
    // One probe at a time: this session exists only to answer one question.
    sp.set_int(lt::settings_pack::active_limit, 2);
    sp.set_int(lt::settings_pack::alert_queue_size, 50000);
    // Keep the probe from becoming an upload source; see below.
    sp.set_int(lt::settings_pack::upload_rate_limit, 16 * 1024);

    lt::session_params params(sp);
    std::unique_ptr<lt::session> ses;
    try {
        ses = std::make_unique<lt::session>(params);
    } catch (const std::exception& e) {
        error = std::string("Could not open the peer network: ") + e.what();
        return false;
    }

    atp.save_path = scratchPath();
    // parse_magnet_uri() hands back default_flags, which contain both
    // auto_managed and paused. A paused torrent contacts no tracker and no
    // peer, so leaving it paused means the probe always waits out its timeout
    // and then wrongly reports that nobody seeds the torrent. auto_managed
    // would instead park it in the queue waiting for a free slot.
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);

    lt::torrent_handle handle;
    try {
        handle = ses->add_torrent(atp);
    } catch (const std::exception& e) {
        error = std::string("Could not query the magnet: ") + e.what();
        return false;
    }
    if (!handle.is_valid()) {
        error = "The magnet link was rejected";
        return false;
    }
    // A listing probe must never contribute to the swarm. The cap is set on
    // the session rather than the handle: set_upload_limit() puts the torrent
    // into upload mode, and a paused upload also stalls the metadata
    // exchange, which per-handle makes every probe fail (measured: 0 peers,
    // every time).

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    // "Done" means the metadata is really there, not merely that libtorrent
    // says so: right after a magnet is added, torrent_file() already returns
    // a placeholder carrying only the infohash, and trusting that would report
    // every torrent as an empty one.
    auto readFiles = [&]() -> bool {
        std::shared_ptr<const lt::torrent_info> ti = handle.torrent_file();
        if (!ti || ti->num_files() <= 0) return false;
        collectFiles(*ti, out);
        if (out.empty()) {
            out.clear();
            return false;
        }
        // The metadata came from a peer, so the row has no .torrent file to
        // hand on. Rebuilding it here is what lets the archive check read its
        // pieces later without asking the swarm for the same metadata again.
        if (torrentBody) {
            torrentBody->clear();
            try {
                lt::add_torrent_params rebuilt;
                rebuilt.ti = std::const_pointer_cast<lt::torrent_info>(ti);
                const std::vector<char> bytes = lt::write_torrent_file_buf(
                    rebuilt, lt::write_flags::allow_missing_piece_layer);
                if (!bytes.empty()) {
                    torrentBody->assign(bytes.data(), bytes.size());
                }
            } catch (...) {
                // A listing does not depend on this; the check simply falls
                // back to exchanging metadata when it is missing.
            }
        }
        return true;
    };

    bool haveMetadata = false;
    // What the probe saw, so a failure can say whether the swarm was empty,
    // the trackers refused, or the DHT never came up.
    int trackerReplies = 0;
    int trackerPeers = 0;
    int trackerErrors = 0;
    bool dhtBootstrapped = false;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            // Drain the queue so the alert mask cannot grow without bound.
            std::vector<lt::alert*> alerts;
            ses->pop_alerts(&alerts);
            for (const lt::alert* a : alerts) {
                if (const auto* reply =
                        lt::alert_cast<lt::tracker_reply_alert>(a)) {
                    ++trackerReplies;
                    trackerPeers += reply->num_peers;
                } else if (lt::alert_cast<lt::tracker_error_alert>(a)) {
                    ++trackerErrors;
                } else if (lt::alert_cast<lt::dht_bootstrap_alert>(a)) {
                    dhtBootstrapped = true;
                }
            }
        }
        if (readFiles()) {
            haveMetadata = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (!haveMetadata) {
        error = describeSwarmFailure(trackerReplies, trackerPeers,
                                     trackerErrors, dhtBootstrapped);
    }

    try {
        ses->remove_torrent(handle, lt::session_handle::delete_files);
    } catch (...) {
        // Best effort: the session destructor tears everything down anyway.
    }
    return haveMetadata;
}

} // namespace

const std::vector<std::string>& TorrentFileList::defaultTrackers() {
    // Chosen because they answered from a plain network when the usual
    // openbittorrent/exodus/demonii set did not: opentrackr over both HTTP and
    // UDP, and two well-known fallbacks.
    static const std::vector<std::string> trackers = {
        "http://tracker.opentrackr.org:1337/announce",
        "udp://tracker.opentrackr.org:1337/announce",
        "udp://open.stealth.si:80/announce",
        "udp://tracker.torrent.eu.org:451/announce",
    };
    return trackers;
}

const char* TorrentFileList::dhtBootstrapNodes() {
    // The two routers libtorrent falls back to are listed first so nothing
    // that used to work stops working; libtorrent.org and transmissionbt are
    // the ones that answered when the first pair was unreachable.
    return "router.bittorrent.com:6881,router.utorrent.com:6881,"
           "dht.libtorrent.org:25401,dht.transmissionbt.com:6881";
}

void TorrentFileList::addDefaultTrackers(std::vector<std::string>& trackers) {
    std::set<std::string> seen(trackers.begin(), trackers.end());
    for (const std::string& tracker : defaultTrackers()) {
        if (seen.insert(tracker).second) trackers.push_back(tracker);
    }
}

std::string TorrentFileList::infoHashOfTorrentFile(const std::string& body) {
    std::vector<TorrentFileEntry> ignored;
    std::string error;
    std::string hash;
    // The hash is filled before the file-list checks, so a torrent with no
    // files still answers this question.
    TorrentFileList::parseTorrentFile(body, ignored, error, &hash);
    return hash;
}

bool TorrentFileList::parseTorrentFile(const std::string& body,
                                       std::vector<TorrentFileEntry>& out,
                                       std::string& error,
                                       std::string* infoHashHex) {
    if (infoHashHex) infoHashHex->clear();

    lt::load_torrent_limits limits;
    // A peer exchange protocol message is 16 bytes; anything that claims to be
    // a torrent far beyond this is not one.
    limits.max_buffer_size = MAX_TORRENT_BYTES;
    limits.max_pieces = 200000;
    limits.max_decode_depth = 100;
    limits.max_decode_tokens = 1000000;

    // libtorrent 2.0 has no error_code overload of load_torrent_buffer();
    // the throwing overload exists in both 2.0 and 2.1, so catch and
    // translate the error instead.
    lt::add_torrent_params atp;
    try {
        atp = lt::load_torrent_buffer(
            lt::span<char const>(body.data(), body.size()), limits);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (!atp.ti) {
        error = "The .torrent file has no metadata";
        return false;
    }
    if (infoHashHex && atp.info_hashes.has_v1()) {
        *infoHashHex = hexOfV1(atp.info_hashes.v1);
    }
    if (atp.ti->num_files() <= 0) {
        error = "The .torrent file lists no files";
        return false;
    }
    collectFiles(*atp.ti, out);
    if (out.empty()) {
        error = "The .torrent file holds no actual content";
        return false;
    }
    return true;
}

std::string TorrentFileList::magnetHash(const std::string& magnet) {
    static const char* marker = "urn:btih:";
    size_t at = magnet.find(marker);
    if (at == std::string::npos) return std::string();
    at += 9;
    size_t end = at;
    while (end < magnet.size() &&
           (std::isalnum(static_cast<unsigned char>(magnet[end])) ||
            magnet[end] == '-'))
        ++end;
    if (end == at) return std::string();

    std::string raw = magnet.substr(at, end - at);
    if (raw.size() == 40) { // already hex
        for (auto& c : raw)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return raw;
    }
    if (raw.size() != 32) return std::string(); // neither hex nor base32

    // Base32 (RFC 4648, uppercase) to hex, so the key is engine independent.
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(40);
    unsigned buffer = 0;
    int bits = 0;
    for (char ch : raw) {
        const char* p = std::strchr(
            alphabet,
            static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (!p) return std::string();
        buffer = (buffer << 5) | static_cast<unsigned>(p - alphabet);
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            unsigned byte = (buffer >> bits) & 0xFFu;
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 0x0Fu]);
        }
        // Drop the bits already emitted, or the shift overflows and the bytes
        // come out rotated.
        buffer &= (1u << bits) - 1u;
    }
    return hex;
}

bool TorrentFileList::resolve(const std::string& magnet,
                              const std::string& torrentUrl,
                              std::vector<TorrentFileEntry>& out,
                              std::string& error, int timeoutSeconds,
                              std::string* torrentBody) {
    out.clear();
    error.clear();
    if (torrentBody) torrentBody->clear();

    const std::string expected = magnetHash(magnet);
    std::string torrentError;

    // The .torrent file first: it is exact, needs no peer connection and
    // costs one HTTP request.
    if (!torrentUrl.empty()) {
        if (!HttpClient::isSecure(torrentUrl)) {
            torrentError = "Only https:// .torrent links are allowed";
        } else {
            std::vector<TorrentFileEntry> fromFile;
            std::string fileError;
            std::string actual;
            if (!resolveFromTorrentUrl(torrentUrl, fromFile, fileError, &actual,
                                       torrentBody)) {
                torrentError = fileError;
            } else if (expected.empty() || actual.empty() ||
                       actual == expected) {
                // Either it matches the magnet, or there is no hash to compare
                // (v2-only torrents carry btmh, not btih).
                out = std::move(fromFile);
                return true;
            } else {
                // A mirror serving a different file must never be shown as
                // this row: the user would be judging someone else's listing.
                torrentError = "the .torrent file is a different torrent";
            }
        }
        // Fall through: a magnet may still work when the .torrent mirror is
        // down, and the hash check above keeps a mismatch harmless.
    }

    if (magnet.empty()) {
        error = torrentError;
        if (error.empty()) {
            error = "This result carries neither a .torrent link nor a magnet";
        }
        return false;
    }

    std::string magnetError;
    if (resolveFromMagnet(magnet, out, magnetError, timeoutSeconds,
                          torrentBody)) {
        return true;
    }
    out.clear();
    if (torrentBody) torrentBody->clear();

    // Report both attempts when both were made: whichever one the user wants
    // to blame, the other is the reason to try again.
    error = magnetError;
    if (!torrentError.empty()) {
        error = "Torrent file: " + torrentError + ". Swarm: " + magnetError;
    }
    return false;
}