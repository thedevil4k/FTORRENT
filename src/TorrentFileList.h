#ifndef TORRENTFILELIST_H
#define TORRENTFILELIST_H

#include "SecurityTypes.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief One file inside a torrent, as the user sees it in the results table.
 *
 * The fields are the shared SecurityFile model so the search row, Add Torrent,
 * the torrent list and Details all analyze exactly the same data. The old
 * name is kept because it is the public type of this fetcher.
 */
using TorrentFileEntry = SecurityFile;

// Where a file list is in its journey. Failed is a normal outcome, not an
// error to recover from: most results never get a swarm answer and the row
// has to stay perfectly usable either way.
enum class FileListState {
    NotRequested, // the user has not opened the row yet
    Queued,       // accepted, waiting for one of the running probes to end
    Loading,      // a worker is fetching the metadata
    Loaded,       // the files are known
    Failed        // the metadata could not be fetched
};

/**
 * @brief Fetches the file list of a search result without touching the UI.
 *
 * Two ways in, tried in this order:
 *   - The result carries a .torrent URL (Nyaa.si and the search engines that
 *     share BitSearch's markup publish one): it is downloaded and parsed
 *     directly. Fast and exact, and its info hash is checked against the
 *     magnet so a stale or mismatched file can never be shown as this row.
 *   - The result carries a magnet: magnet links hold no file list at all,
 *     so the only way to learn it is to ask the swarm. That is done with a
 *     private, throwaway libtorrent session used purely as a leecher. It
 *     uploads almost nothing (16 KB/s cap), is not added to the user's
 *     torrent list, and is torn down as soon as the metadata arrives or the
 *     timeout expires.
 *
 * The swarm path keeps the trackers the magnet came with and adds a few public
 * ones for the trackers that hand out none, and it bootstraps the DHT against
 * explicit nodes: the default routers are unreachable from some networks, and
 * without either a live torrent can look dead.
 *
 * Every call blocks, so it must run on a worker thread, never on the UI
 * thread. It never throws: failures come back through 'error' with the
 * files untouched.
 */
class TorrentFileList {
public:
    // Timeout for the swarm path only; the .torrent download uses HttpClient's
    // own timeouts. A magnet whose peers never answer costs at most this long,
    // in the background, with the table fully usable meanwhile.
    static const int DEFAULT_TIMEOUT_SECONDS = 25;

    // Public trackers added to a probe for the trackers the magnet lacks. Some
    // sites publish trackerless magnets (apibay does exactly that), and the
    // default DHT routers do not answer everywhere, so without these a live
    // swarm would look dead. Used by the probe only: the user's magnet is
    // never rewritten.
    static const std::vector<std::string>& defaultTrackers();

    // Comma-separated host:port list for libtorrent's dht_bootstrap_nodes.
    // Includes the routers libtorrent assumes by default, plus the two that
    // were measured to answer when those do not.
    static const char* dhtBootstrapNodes();

    // Appends defaultTrackers() to 'trackers', skipping duplicates.
    static void addDefaultTrackers(std::vector<std::string>& trackers);

    // Fills 'out' with every file of the torrent. Returns false and fills
    // 'error' with a short reason when the metadata could not be obtained.
    //
    // When 'torrentBody' is given it receives the .torrent bytes the metadata
    // came from: the file that was downloaded, or the one rebuilt from the
    // swarm's own metadata. A later archive check can then read the pieces it
    // needs without a second metadata exchange, which is what made "Check
    // archive" wait on the swarm for a torrent the row had already resolved.
    static bool resolve(const std::string& magnet,
                        const std::string& torrentUrl,
                        std::vector<TorrentFileEntry>& out, std::string& error,
                        int timeoutSeconds = DEFAULT_TIMEOUT_SECONDS,
                        std::string* torrentBody = nullptr);

    // Turns the bytes of a .torrent file into its file list. Exposed on its
    // own because it is the part that can be checked without a network. When
    // 'infoHashHex' is given it receives the file's v1 info hash (lowercase
    // hex), or stays empty for a v2-only torrent.
    static bool parseTorrentFile(const std::string& body,
                                 std::vector<TorrentFileEntry>& out,
                                 std::string& error,
                                 std::string* infoHashHex = nullptr);

    // Lowercase hex of the v1 info hash a .torrent body carries. Empty when
    // the body is not a torrent, or when it only has a v2 hash. Lets the
    // archive probe check a file it downloaded itself against the magnet the
    // row shows, without putting libtorrent's hash type in this header.
    static std::string infoHashOfTorrentFile(const std::string& body);

    // The btih infohash of a magnet link, lowercased hex. Empty when the link
    // carries none. Used as the cache key, so two engines offering the same
    // torrent resolve it once.
    static std::string magnetHash(const std::string& magnet);
};

#endif // TORRENTFILELIST_H