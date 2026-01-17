#ifndef TORRENTITEM_H
#define TORRENTITEM_H

#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <string>
#include <cstdint>

namespace lt = libtorrent;

/**
 * @brief Representa un torrent individual con toda su información
 * 
 * Esta clase encapsula el estado y la información de un torrent,
 * proporcionando una interfaz simplificada para acceder a sus datos.
 */
class TorrentItem {
public:
    enum class State {
        Queued,
        Checking,
        Downloading,
        Seeding,
        Paused,
        Error,
        Complete
    };

    TorrentItem(const lt::torrent_handle& handle);
    
    // Update torrent information
    void update();
    
    // Getters - Basic Info
    std::string getName() const { return m_name; }
    std::string getSavePath() const { return m_savePath; }
    std::string getHash() const { return m_hash; }
    State getState() const { return m_state; }
    std::string getStateString() const;
    
    // Getters - Size Info
    int64_t getTotalSize() const { return m_totalSize; }
    int64_t getDownloaded() const { return m_downloaded; }
    int64_t getUploaded() const { return m_uploaded; }
    double getProgress() const { return m_progress; }
    
    // Getters - Speed Info
    int getDownloadRate() const { return m_downloadRate; }
    int getUploadRate() const { return m_uploadRate; }
    
    // Getters - Peer Info
    int getNumPeers() const { return m_numPeers; }
    int getNumSeeds() const { return m_numSeeds; }
    
    // Getters - Time Info
    int getETA() const; // in seconds
    std::string getETAString() const;
    int64_t getAddedTime() const { return m_addedTime; }
    int64_t getCompletedTime() const { return m_completedTime; }
    
    // Getters - Ratio
    double getRatio() const;
    
    // Handle access
    lt::torrent_handle& getHandle() { return m_handle; }
    const lt::torrent_handle& getHandle() const { return m_handle; }
    bool isValid() const { return m_handle.is_valid(); }
    
    // Data structures for detailed info
    struct TrackerInfo {
        std::string url;
        std::string status;
        std::string message;
    };
    
    struct PeerInfo {
        std::string ip;
        std::string client;
        int downloadRate;
        int uploadRate;
        double progress;
        std::string flags;
    };
    
    struct FileInfo {
        std::string name;
        int64_t size;
        double progress;
        int priority;
    };

    std::vector<TrackerInfo> getTrackers() const;
    std::vector<PeerInfo> getPeers() const;
    std::vector<FileInfo> getFiles() const;

    // Utility
    static std::string formatSize(int64_t bytes);
    static std::string formatSpeed(int bytesPerSecond);
    static std::string formatTime(int seconds);
    static std::string toHex(const lt::sha1_hash& hash);

private:
    lt::torrent_handle m_handle;
    
    // Cached information
    std::string m_name;
    std::string m_savePath;
    std::string m_hash;
    State m_state;
    
    int64_t m_totalSize;
    int64_t m_downloaded;
    int64_t m_uploaded;
    double m_progress;
    
    int m_downloadRate;
    int m_uploadRate;
    
    int m_numPeers;
    int m_numSeeds;
    
    int64_t m_addedTime;
    int64_t m_completedTime;
    
    void updateState(const lt::torrent_status& status);
    State convertState(lt::torrent_status::state_t ltState) const;
};

#endif // TORRENTITEM_H
