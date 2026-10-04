#ifndef TORRENTFILELIST_H
#define TORRENTFILELIST_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief One file inside a torrent, as the user sees it in the results table.
 *
 * The point of this list is to let the user read the extensions before
 * downloading anything, which is the cheapest malware check there is.
 */
struct TorrentFileEntry {
    std::string path;       // Path relative to the torrent root, as-is
    int64_t size = 0;       // Bytes
};

// Where a file list is in its journey. Failed is a normal outcome, not an
// error to recover from: most results never get a swarm answer and the row
// has to stay perfectly usable either way.
enum class FileListState {
    NotRequested,   // the user has not opened the row yet
    Loading,        // a worker is fetching the metadata
    Loaded,         // the files are known
    Failed          // the metadata could not be fetched
};

/**
 * @brief Fetches the file list of a search result without touching the UI.
 *
 * Two ways in, tried in this order:
 *   - The result carries a .torrent URL: it is downloaded and parsed
 *     directly. Fast and exact, but no engine currently fills torrentUrl.
 *   - The result carries a magnet: magnet links hold no file list at all,
 *     so the only way to learn it is to ask the swarm. That is done with a
 *     private, throwaway libtorrent session used purely as a leecher. It
 *     never uploads (upload limited to 1 byte/s), is not added to the
 *     user's torrent list, and is torn down as soon as the metadata
 *     arrives or the timeout expires.
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

    // Fills 'out' with every file of the torrent. Returns false and fills
    // 'error' with a short reason when the metadata could not be obtained.
    static bool resolve(const std::string& magnet,
                        const std::string& torrentUrl,
                        std::vector<TorrentFileEntry>& out,
                        std::string& error,
                        int timeoutSeconds = DEFAULT_TIMEOUT_SECONDS);

    // Turns the bytes of a .torrent file into its file list. Exposed on its
    // own because it is the part that can be checked without a network.
    static bool parseTorrentFile(const std::string& body,
                                 std::vector<TorrentFileEntry>& out,
                                 std::string& error);

    // The btih infohash of a magnet link, lowercased hex. Empty when the link
    // carries none. Used as the cache key, so two engines offering the same
    // torrent resolve it once.
    static std::string magnetHash(const std::string& magnet);
};

#endif // TORRENTFILELIST_H