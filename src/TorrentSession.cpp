#include "TorrentSession.h"
#include <libtorrent/session_params.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <iostream>
#include "SettingsManager.h"

TorrentSession::TorrentSession() 
    : m_initialized(false) {
}

TorrentSession::~TorrentSession() {
    shutdown();
}

bool TorrentSession::initialize() {
    if (m_initialized) {
        return true;
    }

    try {
        auto& sm = SettingsManager::instance();
        sm.load();
        
        // Create session with parameters
        lt::session_params params;
        
        // Alert mask - expand for better diagnostics
        params.settings.set_int(lt::settings_pack::alert_mask, 
            lt::alert_category::error | 
            lt::alert_category::status | 
            lt::alert_category::storage |
            lt::alert_category::tracker |
            lt::alert_category::connect |
            lt::alert_category::peer |
            lt::alert_category::dht |
            lt::alert_category::port_mapping);
        
        // Connectivity and Discovery
        params.settings.set_bool(lt::settings_pack::enable_dht, sm.getDHTEnabled());
        params.settings.set_bool(lt::settings_pack::enable_lsd, sm.getLSDEnabled());
        params.settings.set_bool(lt::settings_pack::enable_upnp, sm.getUPnPEnabled());
        params.settings.set_bool(lt::settings_pack::enable_natpmp, true);
        
        // Default trackers for bootstrapping if needed
        params.settings.set_str(lt::settings_pack::dht_bootstrap_nodes, 
            "router.bittorrent.com:6881,router.utorrent.com:6881,dht.transmissionbt.com:6881,dht.libtorrent.org:25401");
        
        // Network settings
        std::string listen_if = "0.0.0.0:" + std::to_string(sm.getListenPort()) + ",[::]:" + std::to_string(sm.getListenPort());
        params.settings.set_str(lt::settings_pack::listen_interfaces, listen_if);
        
        // Limits
        params.settings.set_int(lt::settings_pack::active_downloads, 20);
        params.settings.set_int(lt::settings_pack::active_seeds, 20);
        params.settings.set_int(lt::settings_pack::active_limit, 50);

        if (sm.getMaxDownloadRate() > 0)
            params.settings.set_int(lt::settings_pack::download_rate_limit, sm.getMaxDownloadRate() * 1024);
        if (sm.getMaxUploadRate() > 0)
            params.settings.set_int(lt::settings_pack::upload_rate_limit, sm.getMaxUploadRate() * 1024);
        
        params.settings.set_int(lt::settings_pack::connections_limit, sm.getMaxConnections());
        
        // User agent
        params.settings.set_str(lt::settings_pack::user_agent, sm.getUserAgent());
        
        // Create session
        m_session = std::make_unique<lt::session>(params);
        
        m_initialized = true;
        std::cout << "TorrentSession initialized with port " << sm.getListenPort() << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize TorrentSession: " << e.what() << std::endl;
        return false;
    }
}

void TorrentSession::shutdown() {
    if (m_initialized && m_session) {
        std::cout << "Shutting down TorrentSession..." << std::endl;
        
        // Pause all torrents
        std::vector<lt::torrent_handle> torrents = m_session->get_torrents();
        for (auto& handle : torrents) {
            if (handle.is_valid()) {
                handle.pause();
            }
        }
        
        m_session.reset();
        m_initialized = false;
    }
}

void TorrentSession::setupSessionSettings() {
    // Additional session configuration can go here
    // For now, using defaults from initialize()
}

bool TorrentSession::addTorrentFile(const std::string& torrentFile, const std::string& savePath) {
    if (!m_initialized || !m_session) {
        std::cerr << "Session not initialized" << std::endl;
        return false;
    }

    try {
        lt::add_torrent_params params;
        params.ti = std::make_shared<lt::torrent_info>(torrentFile);
        params.save_path = savePath;
        
        // Add flags for better handling
        params.flags |= lt::torrent_flags::auto_managed | lt::torrent_flags::duplicate_is_error;
        
        m_session->async_add_torrent(params);
        
        std::cout << "Added torrent: " << params.ti->name() << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to add torrent file: " << e.what() << std::endl;
        return false;
    }
}

bool TorrentSession::addMagnetLink(const std::string& magnetLink, const std::string& savePath) {
    if (!m_initialized || !m_session) {
        std::cerr << "Session not initialized" << std::endl;
        return false;
    }

    try {
        lt::add_torrent_params params;
        params.save_path = savePath;
        
        lt::error_code ec;
        lt::parse_magnet_uri(magnetLink, params, ec);
        
        if (ec) {
            std::cerr << "Invalid magnet link: " << ec.message() << std::endl;
            return false;
        }
        
        params.flags |= lt::torrent_flags::auto_managed | lt::torrent_flags::duplicate_is_error;
        
        m_session->async_add_torrent(params);
        
        std::cout << "Added magnet link" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to add magnet link: " << e.what() << std::endl;
        return false;
    }
}

void TorrentSession::removeTorrent(const lt::torrent_handle& handle, bool deleteFiles) {
    if (!m_initialized || !m_session || !handle.is_valid()) {
        return;
    }

    lt::remove_flags_t flags = {};
    if (deleteFiles) {
        flags |= lt::session::delete_files;
    }
    
    m_session->remove_torrent(handle, flags);
}

void TorrentSession::pauseTorrent(const lt::torrent_handle& handle) {
    if (handle.is_valid()) {
        handle.pause();
    }
}

void TorrentSession::resumeTorrent(const lt::torrent_handle& handle) {
    if (handle.is_valid()) {
        handle.resume();
    }
}

std::vector<lt::torrent_handle> TorrentSession::getTorrents() const {
    if (!m_initialized || !m_session) {
        return {};
    }
    return m_session->get_torrents();
}

std::string TorrentSession::getSessionStats() const {
    if (!m_initialized || !m_session) {
        return "Session not initialized";
    }

    // In libtorrent 2.0, status() is removed. Use metrics system.
    std::string stats = "Session stats (libtorrent 2.0 system)\n";
    // TODO: Implement metrics gathering via alerts
    return stats;
}

int TorrentSession::getDownloadRate() const {
    if (!m_initialized || !m_session) {
        return 0;
    }
    int total = 0;
    auto handles = m_session->get_torrents();
    for (const auto& h : handles) {
        if (h.is_valid()) {
            total += h.status().download_rate;
        }
    }
    return total;
}

int TorrentSession::getUploadRate() const {
    if (!m_initialized || !m_session) {
        return 0;
    }
    int total = 0;
    auto handles = m_session->get_torrents();
    for (const auto& h : handles) {
        if (h.is_valid()) {
            total += h.status().upload_rate;
        }
    }
    return total;
}

void TorrentSession::processAlerts() {
    if (!m_initialized || !m_session) {
        return;
    }

    std::vector<lt::alert*> alerts;
    m_session->pop_alerts(&alerts);
    
    for (lt::alert* alert : alerts) {
        // Handle different alert types
        if (auto* err = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            std::string msg = std::string("Torrent error [") + err->torrent_name() + "]: " + err->message();
            std::cerr << msg << std::endl;
            if (m_errorCallback) m_errorCallback(msg);
        }
        else if (auto* fea = lt::alert_cast<lt::file_error_alert>(alert)) {
            std::string msg = std::string("File error: ") + fea->filename() + " - " + fea->message();
            std::cerr << msg << std::endl;
            if (m_errorCallback) m_errorCallback(msg);
        }
        else if (auto* add = lt::alert_cast<lt::add_torrent_alert>(alert)) {
            if (add->error) {
                std::string msg = std::string("Failed to add torrent: ") + add->error.message();
                std::cerr << msg << std::endl;
                if (m_errorCallback) m_errorCallback(msg);
            } else {
                add->handle.resume(); // Ensure it starts
            }
        }
        else if (auto* ma = lt::alert_cast<lt::metadata_received_alert>(alert)) {
            std::cout << "Metadata received for: " << ma->torrent_name() << std::endl;
        }
        else if (auto* tea = lt::alert_cast<lt::tracker_error_alert>(alert)) {
            // Log tracker errors but don't alert user every time as they are common
            std::cerr << "Tracker error: " << tea->tracker_url() << " - " << tea->error_message() << std::endl;
        }
    }
}
