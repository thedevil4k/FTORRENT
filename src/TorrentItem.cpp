#include "TorrentItem.h"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <sstream>
#include <iomanip>
#include <vector>

TorrentItem::TorrentItem(const lt::torrent_handle& handle)
    : m_handle(handle)
    , m_state(State::Queued)
    , m_totalSize(0)
    , m_downloaded(0)
    , m_uploaded(0)
    , m_progress(0.0)
    , m_downloadRate(0)
    , m_uploadRate(0)
    , m_numPeers(0)
    , m_numSeeds(0)
    , m_addedTime(0)
    , m_completedTime(0)
{
    if (m_handle.is_valid()) {
        update();
    }
}

void TorrentItem::update() {
    if (!m_handle.is_valid()) {
        return;
    }

    try {
        lt::torrent_status status = m_handle.status();
        
        // Basic info
        m_name = status.name;
        
        m_savePath = status.save_path;
        // libtorrent 2.0 uses info_hashes. v1 is sha1_hash
        m_hash = toHex(status.info_hashes.v1);
        
        // Size info
        m_totalSize = status.total_wanted;
        m_downloaded = status.total_wanted_done;
        m_uploaded = status.total_upload;
        m_progress = status.progress;
        
        // Speed info
        m_downloadRate = status.download_rate;
        m_uploadRate = status.upload_rate;
        
        // Peer info
        m_numPeers = status.num_peers;
        m_numSeeds = status.num_seeds;
        
        // State info
        updateState(status);
        
        // Time info
        m_addedTime = status.added_time;

        if (status.completed_time > 0) {
            m_completedTime = status.completed_time;
        }
    } catch (const std::exception& e) {
        // Torrent might have been removed or something else went wrong
        m_state = State::Error;
    }
}

std::string TorrentItem::getStateString() const {
    switch (m_state) {
        case State::Queued:      return "Queued";
        case State::Checking:    return "Checking";
        case State::Downloading: return "Downloading";
        case State::Seeding:     return "Seeding";
        case State::Paused:      return "Paused";
        case State::Error:       return "Error";
        case State::Complete:    return "Complete";
        default:                 return "Unknown";
    }
}

int TorrentItem::getETA() const {
    if (m_state != State::Downloading || m_downloadRate <= 0) {
        return -1; // No ETA
    }
    
    int64_t remaining = m_totalSize - m_downloaded;
    if (remaining <= 0) return 0;
    
    return static_cast<int>(remaining / m_downloadRate);
}

std::string TorrentItem::getETAString() const {
    int eta = getETA();
    if (eta < 0) return "∞";
    if (eta == 0) return "Done";
    
    return formatTime(eta);
}

double TorrentItem::getRatio() const {
    if (m_downloaded <= 0) {
        return m_uploaded > 0 ? 999.0 : 0.0;
    }
    return static_cast<double>(m_uploaded) / m_downloaded;
}

std::string TorrentItem::formatSize(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string TorrentItem::formatSpeed(int bytesPerSecond) {
    if (bytesPerSecond <= 0) return "0 B/s";
    return formatSize(bytesPerSecond) + "/s";
}

std::string TorrentItem::formatTime(int seconds) {
    if (seconds < 0) return "∞";
    
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    
    std::ostringstream oss;
    if (h > 0) {
        oss << h << "h " << m << "m " << s << "s";
    } else if (m > 0) {
        oss << m << "m " << s << "s";
    } else {
        oss << s << "s";
    }
    
    return oss.str();
}

void TorrentItem::updateState(const lt::torrent_status& status) {
    if (status.flags & lt::torrent_flags::paused) {
        m_state = State::Paused;
    } else {
        m_state = convertState(status.state);
        
        // If finished but seeding
        if (m_progress >= 1.0 && m_state == State::Downloading) {
            m_state = State::Complete;
        }
    }
    
    if (status.errc) {
        m_state = State::Error;
    }
}

TorrentItem::State TorrentItem::convertState(lt::torrent_status::state_t ltState) const {
    switch (ltState) {
        case lt::torrent_status::checking_files:
            return State::Checking;
        case lt::torrent_status::downloading_metadata:
        case lt::torrent_status::downloading:
            return State::Downloading;
        case lt::torrent_status::finished:
        case lt::torrent_status::seeding:
            return State::Seeding;
        case lt::torrent_status::checking_resume_data:
            return State::Checking;
        default:
            return State::Queued;
    }
}

std::string TorrentItem::toHex(const lt::sha1_hash& hash) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.clear();
    for (unsigned char c : hash) {
        result.push_back(hex[c >> 4]);
        result.push_back(hex[c & 0xf]);
    }
    return result;
}

std::vector<TorrentItem::TrackerInfo> TorrentItem::getTrackers() const {
    std::vector<TrackerInfo> result;
    if (!m_handle.is_valid()) return result;

    auto trackers = m_handle.trackers();
    for (const auto& t : trackers) {
        TrackerInfo info;
        info.url = t.url;
        info.status = "Working";
        info.message = "";
        result.push_back(info);
    }
    return result;
}

std::vector<TorrentItem::PeerInfo> TorrentItem::getPeers() const {
    std::vector<PeerInfo> result;
    if (!m_handle.is_valid()) return result;

    std::vector<lt::peer_info> peers;
    m_handle.get_peer_info(peers);

    for (const auto& p : peers) {
        PeerInfo info;
        info.ip = p.ip.address().to_string();
        info.client = p.client;
        info.downloadRate = p.down_speed;
        info.uploadRate = p.up_speed;
        info.progress = p.progress;
        
        // Flags
        if (p.flags & lt::peer_info::interesting) info.flags += "I";
        if (p.flags & lt::peer_info::choked) info.flags += "C";
        if (p.flags & lt::peer_info::remote_interested) info.flags += "i";
        if (p.flags & lt::peer_info::remote_choked) info.flags += "c";
        if (p.flags & lt::peer_info::supports_extensions) info.flags += "e";
        if (p.flags & lt::peer_info::local_connection) info.flags += "L";
        
        result.push_back(info);
    }
    return result;
}

std::vector<TorrentItem::FileInfo> TorrentItem::getFiles() const {
    std::vector<FileInfo> result;
    if (!m_handle.is_valid()) return result;

    auto info = m_handle.torrent_file();
    if (!info) return result;

    lt::file_storage const& fs = info->files();
    std::vector<int64_t> file_progress;
    m_handle.file_progress(file_progress);

    for (int i = 0; i < fs.num_files(); ++i) {
        FileInfo fi;
        fi.name = fs.file_name(lt::file_index_t(i)).to_string();
        fi.size = fs.file_size(lt::file_index_t(i));
        
        if (fi.size > 0 && i < (int)file_progress.size()) {
            fi.progress = (double)file_progress[i] / fi.size;
        } else {
            fi.progress = 0.0;
        }
        
        fi.priority = (int)static_cast<std::uint8_t>(m_handle.file_priority(lt::file_index_t(i)));
        result.push_back(fi);
    }
    return result;
}
