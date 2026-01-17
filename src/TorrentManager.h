#ifndef TORRENTMANAGER_H
#define TORRENTMANAGER_H

#include "TorrentSession.h"
#include "TorrentItem.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

/**
 * @brief Gestor central de torrents que coordina la sesión y la lista de torrents
 * 
 * Esta clase actúa como fachada entre la sesión de libtorrent y la interfaz de usuario,
 * manteniendo una lista de TorrentItem y notificando cambios mediante callbacks.
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
    bool isInitialized() const { return m_initialized; }

    // Torrent operations
    bool addTorrentFile(const std::string& torrentFile, const std::string& savePath);
    bool addMagnetLink(const std::string& magnetLink, const std::string& savePath);
    void removeTorrent(const std::string& hash, bool deleteFiles = false);
    void pauseTorrent(const std::string& hash);
    void resumeTorrent(const std::string& hash);
    void pauseAll();
    void resumeAll();

    // Torrent queries
    TorrentItem* getTorrent(const std::string& hash);
    const TorrentItem* getTorrent(const std::string& hash) const;
    std::vector<TorrentItem*> getAllTorrents();
    std::vector<const TorrentItem*> getAllTorrents() const;
    int getTorrentCount() const { return m_torrents.size(); }

    // Statistics
    int getTotalDownloadRate() const;
    int getTotalUploadRate() const;
    int getActiveTorrentsCount() const;
    std::string getSessionStats() const;

    // Update - should be called regularly
    void update();
    void processAlerts();

    // Callbacks
    void setOnTorrentAdded(TorrentAddedCallback callback) { m_onTorrentAdded = callback; }
    void setOnTorrentRemoved(TorrentRemovedCallback callback) { m_onTorrentRemoved = callback; }
    void setOnTorrentUpdated(TorrentUpdatedCallback callback) { m_onTorrentUpdated = callback; }
    void setOnStatsUpdated(StatsUpdatedCallback callback) { m_onStatsUpdated = callback; }
    void setOnError(ErrorCallback callback) { m_onError = callback; }

private:
    std::unique_ptr<TorrentSession> m_session;
    std::vector<std::unique_ptr<TorrentItem>> m_torrents;
    bool m_initialized;

    // Callbacks
    TorrentAddedCallback m_onTorrentAdded;
    TorrentRemovedCallback m_onTorrentRemoved;
    TorrentUpdatedCallback m_onTorrentUpdated;
    StatsUpdatedCallback m_onStatsUpdated;
    ErrorCallback m_onError;

    // Helper methods
    void syncTorrents();
    TorrentItem* findTorrent(const std::string& hash);
    const TorrentItem* findTorrent(const std::string& hash) const;
    void notifyTorrentAdded(TorrentItem* item);
    void notifyTorrentRemoved(const std::string& hash);
    void notifyTorrentUpdated(TorrentItem* item);
    void notifyStatsUpdated();
    void notifyError(const std::string& error);
};

#endif // TORRENTMANAGER_H
