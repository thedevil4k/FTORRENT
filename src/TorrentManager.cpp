#include "TorrentManager.h"
#include <algorithm>
#include <iostream>
#include <libtorrent/hex.hpp>
#include <chrono>
#include <fstream>
#include <filesystem>

TorrentManager::TorrentManager()
    : m_initialized(false)
    , m_running(false)
{
    m_session = std::make_unique<TorrentSession>();
}

TorrentManager::~TorrentManager() {
    shutdown();
}

bool TorrentManager::initialize() {
    if (m_initialized.load()) {
        return true;
    }

    if (!m_session->initialize()) {
        notifyError("Failed to initialize torrent session");
        return false;
    }

    m_session->setErrorCallback([this](const std::string& error) {
        notifyError(error);
    });

    m_running.store(true);
    m_initialized.store(true);
    
    // Get number of hardware threads for info
    size_t numThreads = std::max(2u, std::min(8u, std::thread::hardware_concurrency()));
    std::cout << "TorrentManager initialized (using " << numThreads << " CPU cores available)" << std::endl;
    
    // Load resident torrents (persistence)
    m_session->loadResidentTorrents();
    
    return true;
}

void TorrentManager::shutdown() {
    if (!m_initialized.load()) {
        return;
    }

    // Signal shutdown
    m_running.store(false);

    // Clear torrents
    {
        std::lock_guard<std::mutex> lock(m_torrentsMutex);
        m_torrents.clear();
    }
    
    if (m_session) {
        m_session->shutdown();
    }

    m_initialized.store(false);
    std::cout << "TorrentManager shutdown complete" << std::endl;
}

// Async operations for non-blocking UI
std::future<bool> TorrentManager::addTorrentFileAsync(const std::string& torrentFile, const std::string& savePath) {
    // For now, execute synchronously but return as future
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    bool result = addTorrentFile(torrentFile, savePath);
    promise->set_value(result);
    
    return future;
}

std::future<bool> TorrentManager::addMagnetLinkAsync(const std::string& magnetLink, const std::string& savePath) {
    // For now, execute synchronously but return as future
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    bool result = addMagnetLink(magnetLink, savePath);
    promise->set_value(result);
    
    return future;
}

// Synchronous operations
bool TorrentManager::addTorrentFile(const std::string& torrentFile, const std::string& savePath) {
    if (!m_initialized.load()) {
        notifyError("Session not initialized");
        return false;
    }

    bool success = m_session->addTorrentFile(torrentFile, savePath);
    
    if (success) {
        std::lock_guard<std::mutex> lock(m_torrentsMutex);
        syncTorrentsInternal();
    }

    return success;
}

bool TorrentManager::addMagnetLink(const std::string& magnetLink, const std::string& savePath) {
    if (!m_initialized.load()) {
        notifyError("Session not initialized");
        return false;
    }

    bool success = m_session->addMagnetLink(magnetLink, savePath);
    
    if (success) {
        std::lock_guard<std::mutex> lock(m_torrentsMutex);
        syncTorrentsInternal();
    }

    return success;
}

void TorrentManager::removeTorrent(const std::string& hash, bool deleteFiles) {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    
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
    m_session->removeResumeData(hash);
    
    // Notify before erasing while pointer is still valid
    notifyTorrentRemoved(hash);

    // Remove from our list (this deletes the object)
    m_torrents.erase(it);
}

void TorrentManager::pauseTorrent(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    
    auto* torrent = findTorrentInternal(hash);
    if (torrent) {
        m_session->pauseTorrent(torrent->getHandle());
        torrent->update();
    }
}

void TorrentManager::resumeTorrent(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    
    auto* torrent = findTorrentInternal(hash);
    if (torrent) {
        m_session->resumeTorrent(torrent->getHandle());
        torrent->update();
    }
}

void TorrentManager::pauseAll() {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    
    for (auto& torrent : m_torrents) {
        m_session->pauseTorrent(torrent->getHandle());
        torrent->update();
    }
}

void TorrentManager::resumeAll() {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    
    for (auto& torrent : m_torrents) {
        m_session->resumeTorrent(torrent->getHandle());
        torrent->update();
    }
}

void TorrentManager::setRateLimits(int downloadKBps, int uploadKBps) {
    if (m_session) {
        m_session->setRateLimits(downloadKBps, uploadKBps);
    }
}

TorrentItem* TorrentManager::getTorrent(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    return findTorrentInternal(hash);
}

const TorrentItem* TorrentManager::getTorrent(const std::string& hash) const {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    return findTorrentInternal(hash);
}

std::vector<TorrentItem*> TorrentManager::getAllTorrents() {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    std::vector<TorrentItem*> result;
    result.reserve(m_torrents.size());
    
    for (auto& torrent : m_torrents) {
        result.push_back(torrent.get());
    }
    
    return result;
}

std::vector<const TorrentItem*> TorrentManager::getAllTorrents() const {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    std::vector<const TorrentItem*> result;
    result.reserve(m_torrents.size());
    
    for (const auto& torrent : m_torrents) {
        result.push_back(torrent.get());
    }
    
    return result;
}

int TorrentManager::getTorrentCount() const {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    return static_cast<int>(m_torrents.size());
}

int TorrentManager::getTotalDownloadRate() const {
    if (!m_initialized.load()) {
        return 0;
    }
    return m_session->getDownloadRate();
}

int TorrentManager::getTotalUploadRate() const {
    if (!m_initialized.load()) {
        return 0;
    }
    return m_session->getUploadRate();
}

int TorrentManager::getActiveTorrentsCount() const {
    std::lock_guard<std::mutex> lock(m_torrentsMutex);
    return std::count_if(m_torrents.begin(), m_torrents.end(),
        [](const std::unique_ptr<TorrentItem>& item) {
            auto state = item->getState();
            return state == TorrentItem::State::Downloading || 
                   state == TorrentItem::State::Seeding;
        });
}

std::string TorrentManager::getSessionStats() const {
    if (!m_initialized.load()) {
        return "Session not initialized";
    }
    return m_session->getSessionStats();
}

void TorrentManager::update() {
    if (!m_initialized.load()) {
        return;
    }

    // Process alerts from libtorrent
    m_session->processAlerts();

    // Sync and update torrents
    {
        std::lock_guard<std::mutex> lock(m_torrentsMutex);
        
        // Sync with libtorrent session
        syncTorrentsInternal();
        
        // Update all torrent items
        for (auto& torrent : m_torrents) {
            torrent->update();
            notifyTorrentUpdated(torrent.get());
        }
    }

    // Periodic resume data saving (every 30 seconds)
    static auto lastSave = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - lastSave > std::chrono::seconds(30)) {
        m_session->triggerSaveResumeData();
        lastSave = std::chrono::steady_clock::now();
    }

    // Notify UI about updates (outside lock to prevent deadlock)
    notifyStatsUpdated();
}

void TorrentManager::processAlerts() {
    if (!m_initialized.load()) {
        return;
    }
    m_session->processAlerts();
}

// Callback setters (thread-safe)
void TorrentManager::setOnTorrentAdded(TorrentAddedCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onTorrentAdded = callback;
}

void TorrentManager::setOnTorrentRemoved(TorrentRemovedCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onTorrentRemoved = callback;
}

void TorrentManager::setOnTorrentUpdated(TorrentUpdatedCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onTorrentUpdated = callback;
}

void TorrentManager::setOnStatsUpdated(StatsUpdatedCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onStatsUpdated = callback;
}

void TorrentManager::setOnError(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onError = callback;
}

// ====================
// PRIVATE METHODS
// ====================

void TorrentManager::syncTorrentsInternal() {
    // IMPORTANT: Caller must hold m_torrentsMutex
    
    if (!m_initialized.load()) {
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
    
    // 2. Remove identified torrents
    for (const auto& hash : toRemove) {
        notifyTorrentRemoved(hash);
        m_torrents.erase(
            std::remove_if(m_torrents.begin(), m_torrents.end(),
                [&hash](const std::unique_ptr<TorrentItem>& item) {
                    return item && item->getHash() == hash;
                }),
            m_torrents.end()
        );
    }

    // 3. Add new torrents
    for (const auto& handle : handles) {
        if (!handle.is_valid()) {
            continue;
        }

        std::string hash = TorrentItem::toHex(handle.info_hashes().v1);
        
        if (!findTorrentInternal(hash)) {
            // Add extra trackers for better connectivity
            std::ifstream trackerFile("trackersadd.txt");
            if (trackerFile.is_open()) {
                std::string tracker;
                while (std::getline(trackerFile, tracker)) {
                    if (!tracker.empty() && tracker.length() > 5) {
                        lt::announce_entry ae(tracker);
                        handle.add_tracker(ae);
                    }
                }
                trackerFile.close();
                handle.force_reannounce();
            }

            auto newTorrent = std::make_unique<TorrentItem>(handle);
            TorrentItem* ptr = newTorrent.get();
            m_torrents.push_back(std::move(newTorrent));
            notifyTorrentAdded(ptr);
        }
    }
}

TorrentItem* TorrentManager::findTorrentInternal(const std::string& hash) {
    // IMPORTANT: Caller must hold m_torrentsMutex
    
    auto it = std::find_if(m_torrents.begin(), m_torrents.end(),
        [&hash](const std::unique_ptr<TorrentItem>& item) {
            return item->getHash() == hash;
        });
    
    return it != m_torrents.end() ? it->get() : nullptr;
}

const TorrentItem* TorrentManager::findTorrentInternal(const std::string& hash) const {
    // IMPORTANT: Caller must hold m_torrentsMutex
    
    auto it = std::find_if(m_torrents.begin(), m_torrents.end(),
        [&hash](const std::unique_ptr<TorrentItem>& item) {
            return item->getHash() == hash;
        });
    
    return it != m_torrents.end() ? it->get() : nullptr;
}

void TorrentManager::notifyTorrentAdded(TorrentItem* item) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onTorrentAdded) {
        m_onTorrentAdded(item);
    }
}

void TorrentManager::notifyTorrentRemoved(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onTorrentRemoved) {
        m_onTorrentRemoved(hash);
    }
}

void TorrentManager::notifyTorrentUpdated(TorrentItem* item) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onTorrentUpdated) {
        m_onTorrentUpdated(item);
    }
}

void TorrentManager::notifyStatsUpdated() {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onStatsUpdated) {
        m_onStatsUpdated();
    }
}

void TorrentManager::notifyError(const std::string& error) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    if (m_onError) {
        m_onError(error);
    }
    std::cerr << "TorrentManager Error: " << error << std::endl;
}
