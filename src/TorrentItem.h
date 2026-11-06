#ifndef TORRENTITEM_H
#define TORRENTITEM_H

#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include "SecurityTypes.h"
#include <string>
#include <cstdint>
#include <mutex>
#include <vector>

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
    std::string getName() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_name;
    }
    std::string getSavePath() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_savePath;
    }
    std::string getHash() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hash;
    }
    State getState() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_state;
    }
    std::string getStateString() const; // Implemented in cpp

    // Getters - Size Info
    int64_t getTotalSize() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalSize;
    }
    int64_t getDownloaded() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_downloaded;
    }
    int64_t getUploaded() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_uploaded;
    }
    double getProgress() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_progress;
    }

    // Getters - Speed Info
    int getDownloadRate() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_downloadRate;
    }
    int getUploadRate() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_uploadRate;
    }

    // Getters - Peer Info
    int getNumPeers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_numPeers;
    }
    int getNumSeeds() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_numSeeds;
    }

    // Getters - Time Info
    int getETA() const; // in seconds, impl in cpp needs lock? It accesses
                        // members, so yes.
    std::string getETAString() const;
    int64_t getAddedTime() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_addedTime;
    }
    int64_t getCompletedTime() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_completedTime;
    }

    // Getters - Ratio
    double getRatio() const;

    // Handle access - returns safe handle copy
    lt::torrent_handle getHandle() { return m_handle; }
    const lt::torrent_handle getHandle() const { return m_handle; }
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
        // What the network monitor needs beyond the Details list.
        int port = 0;
        bool outgoing = false;  // We opened the connection
        bool encrypted = false; // The peer link is encrypted
    };

    struct FileInfo {
        std::string name;
        std::string path; // Path relative to the save path, with folders
        int64_t size;
        double progress;
        bool complete; // Every byte of this file is on disk
        int priority;
        int fileIndex = -1; // libtorrent index, pad files keep their gaps
        bool hidden = false;
        bool executable = false;
        bool symlink = false;
        bool absolutePath = false;
        std::string symlinkPath;
        // Whole-file SHA-256 from v2 metadata, when the metadata carries one.
        std::string sha256;
        bool sha256Complete = false;
    };

    std::vector<TrackerInfo> getTrackers() const;
    std::vector<PeerInfo> getPeers() const;
    std::vector<FileInfo> getFiles() const;
    // Same files in the shared model the metadata analyzer consumes.
    std::vector<SecurityFile> getSecurityFiles() const;

    // Utility
    static std::string toHex(const lt::sha1_hash& hash);

private:
    lt::torrent_handle m_handle;
    mutable std::mutex m_mutex;

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
