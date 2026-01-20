#ifndef TORRENTMANAGER_H
#define TORRENTMANAGER_H

#include "TorrentSession.h"
#include "TorrentItem.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <mutex>
#include <atomic>
#include <future>

/**
 * @brief Thread-safe torrent manager with stable architecture
 * 
 * This class provides a thread-safe interface between libtorrent and the UI.
 * Features:
 * - Thread-safe operations with mutex protection
 * - Async API support with std::future
 * - Callbacks to UI for state changes
 * - libtorrent handles internal multi-threading for network/disk I/O
 */
class TorrentManager {
public:
    // Callback types
    using TorrentAddedCallback = std::function<void(TorrentItem*)>;
    using TorrentRemovedCallback = std::function<void(const std::string& hash)>;
    using TorrentUpdatedCallback = std::function<void(TorrentItem*)>;
    using StatsUpdatedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    TorrentManager();
    ~TorrentManager();

    // Initialization
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized.load(); }

    // Torrent operations (all thread-safe)
    std::future<bool> addTorrentFileAsync(const std::string& torrentFile, const std::string& savePath);
    std::future<bool> addMagnetLinkAsync(const std::string& magnetLink, const std::string& savePath);
    bool addTorrentFile(const std::string& torrentFile, const std::string& savePath);
    bool addMagnetLink(const std::string& magnetLink, const std::string& savePath);
    void removeTorrent(const std::string& hash, bool deleteFiles = false);
    void pauseTorrent(const std::string& hash);
    void resumeTorrent(const std::string& hash);
    void pauseAll();
    void resumeAll();

    // Torrent queries (thread-safe with mutex locking)
    TorrentItem* getTorrent(const std::string& hash);
    const TorrentItem* getTorrent(const std::string& hash) const;
    std::vector<TorrentItem*> getAllTorrents();
    std::vector<const TorrentItem*> getAllTorrents() const;
    int getTorrentCount() const;

    // Statistics (thread-safe)
    int getTotalDownloadRate() const;
    int getTotalUploadRate() const;
    int getActiveTorrentsCount() const;
    std::string getSessionStats() const;

    // Update - called from UI thread timer (thread-safe)
    void update();
    void processAlerts();

    // Callbacks (called from UI thread)
    void setOnTorrentAdded(TorrentAddedCallback callback);
    void setOnTorrentRemoved(TorrentRemovedCallback callback);
    void setOnTorrentUpdated(TorrentUpdatedCallback callback);
    void setOnStatsUpdated(StatsUpdatedCallback callback);
    void setOnError(ErrorCallback callback);

private:
    // Core data
    std::unique_ptr<TorrentSession> m_session;
    std::vector<std::unique_ptr<TorrentItem>> m_torrents;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_running;

    // Thread synchronization
    mutable std::mutex m_torrentsMutex;
    mutable std::mutex m_callbacksMutex;

    // Callbacks (protected by m_callbacksMutex)
    TorrentAddedCallback m_onTorrentAdded;
    TorrentRemovedCallback m_onTorrentRemoved;
    TorrentUpdatedCallback m_onTorrentUpdated;
    StatsUpdatedCallback m_onStatsUpdated;
    ErrorCallback m_onError;

    // Helper methods (require mutex to be held by caller)
    void syncTorrentsInternal();
    TorrentItem* findTorrentInternal(const std::string& hash);
    const TorrentItem* findTorrentInternal(const std::string& hash) const;
    
    // Thread-safe notification methods
    void notifyTorrentAdded(TorrentItem* item);
    void notifyTorrentRemoved(const std::string& hash);
    void notifyTorrentUpdated(TorrentItem* item);
    void notifyStatsUpdated();
    void notifyError(const std::string& error);
};

#endif // TORRENTMANAGER_H
