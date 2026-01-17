#include "TorrentManager.h"
#include <algorithm>
#include <iostream>
#include <libtorrent/hex.hpp>

TorrentManager::TorrentManager()
    : m_initialized(false)
{
    m_session = std::make_unique<TorrentSession>();
}

TorrentManager::~TorrentManager() {
    shutdown();
}

bool TorrentManager::initialize() {
    if (m_initialized) {
        return true;
    }

    if (!m_session->initialize()) {
        notifyError("Failed to initialize torrent session");
        return false;
    }

    m_session->setErrorCallback([this](const std::string& error) {
        notifyError(error);
    });

    m_initialized = true;
    return true;
}

void TorrentManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_torrents.clear();
    
    if (m_session) {
        m_session->shutdown();
    }

    m_initialized = false;
}

bool TorrentManager::addTorrentFile(const std::string& torrentFile, const std::string& savePath) {
    if (!m_initialized) {
        notifyError("Session not initialized");
        return false;
    }

    bool success = m_session->addTorrentFile(torrentFile, savePath);
    
    if (success) {
        // Sync torrents to update the list
        syncTorrents();
    }

    return success;
}

bool TorrentManager::addMagnetLink(const std::string& magnetLink, const std::string& savePath) {
    if (!m_initialized) {
        notifyError("Session not initialized");
        return false;
    }

    bool success = m_session->addMagnetLink(magnetLink, savePath);
    
    if (success) {
        // Sync torrents to update the list
        syncTorrents();
    }

    return success;
}

void TorrentManager::removeTorrent(const std::string& hash, bool deleteFiles) {
    auto it = std::find_if(m_torrents.begin(), m_torrents.end(),
        [&hash](const std::unique_ptr<TorrentItem>& item) {
            return item && item->getHash() == hash;
        });

    if (it == m_torrents.end()) {
        return;
    }

    // Keep handle for removal
    lt::torrent_handle handle = (*it)->getHandle();
    
    // Remove from session
    m_session->removeTorrent(handle, deleteFiles);
    
    // Notify before deletion (optional, but safer as hash is still valid)
    notifyTorrentRemoved(hash);
    
    // Remove from our list (this deletes the object)
    m_torrents.erase(it);
}

void TorrentManager::pauseTorrent(const std::string& hash) {
    auto* torrent = findTorrent(hash);
    if (torrent) {
        m_session->pauseTorrent(torrent->getHandle());
        torrent->update();
        notifyTorrentUpdated(torrent);
    }
}

void TorrentManager::resumeTorrent(const std::string& hash) {
    auto* torrent = findTorrent(hash);
    if (torrent) {
        m_session->resumeTorrent(torrent->getHandle());
        torrent->update();
        notifyTorrentUpdated(torrent);
    }
}

void TorrentManager::pauseAll() {
    for (auto& torrent : m_torrents) {
        m_session->pauseTorrent(torrent->getHandle());
        torrent->update();
        notifyTorrentUpdated(torrent.get());
    }
}

void TorrentManager::resumeAll() {
    for (auto& torrent : m_torrents) {
        m_session->resumeTorrent(torrent->getHandle());
        torrent->update();
        notifyTorrentUpdated(torrent.get());
    }
}

TorrentItem* TorrentManager::getTorrent(const std::string& hash) {
    return findTorrent(hash);
}

const TorrentItem* TorrentManager::getTorrent(const std::string& hash) const {
    return findTorrent(hash);
}

std::vector<TorrentItem*> TorrentManager::getAllTorrents() {
    std::vector<TorrentItem*> result;
    result.reserve(m_torrents.size());
    
    for (auto& torrent : m_torrents) {
        result.push_back(torrent.get());
    }
    
    return result;
}

std::vector<const TorrentItem*> TorrentManager::getAllTorrents() const {
    std::vector<const TorrentItem*> result;
    result.reserve(m_torrents.size());
    
    for (const auto& torrent : m_torrents) {
        result.push_back(torrent.get());
    }
    
    return result;
}

int TorrentManager::getTotalDownloadRate() const {
    if (!m_initialized) {
        return 0;
    }
    return m_session->getDownloadRate();
}

int TorrentManager::getTotalUploadRate() const {
    if (!m_initialized) {
        return 0;
    }
    return m_session->getUploadRate();
}

int TorrentManager::getActiveTorrentsCount() const {
    return std::count_if(m_torrents.begin(), m_torrents.end(),
        [](const std::unique_ptr<TorrentItem>& item) {
            auto state = item->getState();
            return state == TorrentItem::State::Downloading || 
                   state == TorrentItem::State::Seeding;
        });
}

std::string TorrentManager::getSessionStats() const {
    if (!m_initialized) {
        return "Session not initialized";
    }
    return m_session->getSessionStats();
}

void TorrentManager::update() {
    if (!m_initialized) {
        return;
    }

    // Process alerts first
    processAlerts();

    // Sync torrents (detect new/removed torrents)
    syncTorrents();

    // Update all torrent items
    for (auto& torrent : m_torrents) {
        torrent->update();
        notifyTorrentUpdated(torrent.get());
    }

    notifyStatsUpdated();
}

void TorrentManager::processAlerts() {
    if (!m_initialized) {
        return;
    }
    m_session->processAlerts();
}

void TorrentManager::syncTorrents() {
    if (!m_initialized) {
        return;
    }

    auto handles = m_session->getTorrents();
    
    // 1. Identify torrents to remove
    std::vector<std::string> toRemove;
    for (const auto& item : m_torrents) {
        if (!item) continue;
        
        std::string hash = item->getHash();
        auto it = std::find_if(handles.begin(), handles.end(),
            [&hash](const lt::torrent_handle& h) {
                return h.is_valid() && 
                       TorrentItem::toHex(h.info_hashes().v1) == hash;
            });
            
        if (it == handles.end()) {
            toRemove.push_back(hash);
        }
    }
    
    // 2. Remove identified torrents and notify
    for (const auto& hash : toRemove) {
        m_torrents.erase(
            std::remove_if(m_torrents.begin(), m_torrents.end(),
                [&hash](const std::unique_ptr<TorrentItem>& item) {
                    return item && item->getHash() == hash;
                }),
            m_torrents.end()
        );
        notifyTorrentRemoved(hash);
    }

    // 3. Add new torrents
    for (const auto& handle : handles) {
        if (!handle.is_valid()) {
            continue;
        }

        std::string hash = TorrentItem::toHex(handle.info_hashes().v1);
        
        if (!findTorrent(hash)) {
            auto newTorrent = std::make_unique<TorrentItem>(handle);
            auto* ptr = newTorrent.get();
            m_torrents.push_back(std::move(newTorrent));
            notifyTorrentAdded(ptr);
        }
    }
}

TorrentItem* TorrentManager::findTorrent(const std::string& hash) {
    auto it = std::find_if(m_torrents.begin(), m_torrents.end(),
        [&hash](const std::unique_ptr<TorrentItem>& item) {
            return item->getHash() == hash;
        });
    
    return it != m_torrents.end() ? it->get() : nullptr;
}

const TorrentItem* TorrentManager::findTorrent(const std::string& hash) const {
    auto it = std::find_if(m_torrents.begin(), m_torrents.end(),
        [&hash](const std::unique_ptr<TorrentItem>& item) {
            return item->getHash() == hash;
        });
    
    return it != m_torrents.end() ? it->get() : nullptr;
}

void TorrentManager::notifyTorrentAdded(TorrentItem* item) {
    if (m_onTorrentAdded) {
        m_onTorrentAdded(item);
    }
}

void TorrentManager::notifyTorrentRemoved(const std::string& hash) {
    if (m_onTorrentRemoved) {
        m_onTorrentRemoved(hash);
    }
}

void TorrentManager::notifyTorrentUpdated(TorrentItem* item) {
    if (m_onTorrentUpdated) {
        m_onTorrentUpdated(item);
    }
}

void TorrentManager::notifyStatsUpdated() {
    if (m_onStatsUpdated) {
        m_onStatsUpdated();
    }
}

void TorrentManager::notifyError(const std::string& error) {
    if (m_onError) {
        m_onError(error);
    }
    std::cerr << "TorrentManager Error: " << error << std::endl;
}
