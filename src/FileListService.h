#ifndef FILELISTSERVICE_H
#define FILELISTSERVICE_H

#include "TorrentFileList.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Knows which torrents' file lists are already known, and fetches the
 *        ones that are not.
 *
 * This is the single owner of everything behind the expand arrow in the search
 * results: the cache, the concurrency limit, and the lifetime of the threads
 * doing the fetching. A table owns a service, asks it for a list, and is told
 * when the answer lands.
 *
 * It deliberately knows nothing about widgets, rows or search results: it is
 * keyed by an opaque string the caller chooses, so the same service would back
 * any other surface that wants to show what a torrent downloads.
 *
 * Data flows one way: request() goes in on the UI thread, and the completion
 * callback comes back out on the UI thread. Nothing else crosses the boundary.
 * That is also why the completion is never called after the service is gone --
 * the answer is dropped instead of delivered into a dead listener.
 */
class FileListService {
public:
    // What is known about one torrent's file list. FileListState itself lives
    // in TorrentFileList.h, next to the fetch it describes.
    struct Entry {
        FileListState state = FileListState::NotRequested;
        std::vector<TorrentFileEntry> files;
        std::string error;
    };

    // Runs on the UI thread when a request finishes, and never after this
    // service has been destroyed.
    using Completion = std::function<void(const Entry&)>;

    FileListService();
    ~FileListService();

    FileListService(const FileListService&) = delete;
    FileListService& operator=(const FileListService&) = delete;

    // What is known right now. Starts nothing.
    Entry find(const std::string& key) const;

    // Starts fetching unless the list is already known or already on its way.
    // Returns whether it actually started.
    bool request(const std::string& key,
                 const std::string& magnet,
                 const std::string& torrentUrl,
                 const Completion& done);

    // Fetches currently running.
    int pending() const;

    // Forgets every remembered list. In-flight fetches still deliver when they
    // land; they simply find nothing to update.
    void clear();

private:
    // Everything a worker needs once the owner may be gone. Held by shared
    // pointer, so a worker can never reach through a destroyed service: it
    // only ever touches this block, and it checks alive before delivering.
    struct Shared {
        int maxCached = 0;             // set once, at construction
        int maxInFlight = 0;
        std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
        std::deque<std::string> order;       // insertion order, for the cap
        std::atomic<int> inFlight{0};
        std::atomic<bool> alive{true};
    };

    // Carried by a worker to the UI thread; freed by deliver() either way.
    struct Delivery {
        std::shared_ptr<Shared> shared;
        std::string key;
        Entry entry;
        Completion done;
    };

    // Entry point for Fl::awake(), so it can reach the private members.
    static void deliver(void* data);

    std::shared_ptr<Shared> m_shared;
};

#endif // FILELISTSERVICE_H