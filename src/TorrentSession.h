#ifndef TORRENTSESSION_H
#define TORRENTSESSION_H

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <string>
#include <vector>
#include <memory>

#include <functional>

namespace lt = libtorrent;

class TorrentSession {
public:
    using ErrorCallback = std::function<void(const std::string&)>;

    TorrentSession();
    ~TorrentSession();

    // Session management
    bool initialize();
    void shutdown();
    
    // Torrent operations
    bool addTorrentFile(const std::string& torrentFile, const std::string& savePath);
    bool addMagnetLink(const std::string& magnetLink, const std::string& savePath);
    void removeTorrent(const lt::torrent_handle& handle, bool deleteFiles = false);
    void pauseTorrent(const lt::torrent_handle& handle);
    void resumeTorrent(const lt::torrent_handle& handle);
    
    // Rate limiting
    void setRateLimits(int downloadKBps, int uploadKBps);
    void setRamMode(int mode);
    
    // Persistence
    void triggerSaveResumeData();
    void loadResidentTorrents();
    void removeResumeData(const std::string& hash);
    
    // Information getters
    std::vector<lt::torrent_handle> getTorrents() const;
    std::string getSessionStats() const;
    int getDownloadRate() const;
    int getUploadRate() const;
    
    // Process alerts
    void processAlerts();
    
    // Callbacks
    void setErrorCallback(ErrorCallback cb) { m_errorCallback = cb; }

private:
    std::unique_ptr<lt::session> m_session;
    bool m_initialized;
    ErrorCallback m_errorCallback;
    
    void setupSessionSettings();
    void writeResumeData(const lt::save_resume_data_alert* rd);
    std::string getResumeDataPath() const;
};

#endif // TORRENTSESSION_H
