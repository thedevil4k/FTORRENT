#include "MainWindow.h"
#include "CreateTorrentDialog.h"
#include "RemoveConfirmDialog.h"
#include "SettingsManager.h"
#include <FL/fl_ask.H>
#include <algorithm>
#include <string>
#include <vector>

bool MainWindow::showAddTorrentDialog(const std::string& prefilledPath, const std::string& prefilledMagnet,
                                       const std::vector<TorrentFileEntry>& prefilledFiles,
                                       const std::string& prefilledTorrentUrl,
                                       const std::string& prefilledBody) {
    AddTorrentDialog* dlg = new AddTorrentDialog();

    if (!prefilledPath.empty()) {
        dlg->setTorrentPath(prefilledPath);
    }
    if (!prefilledTorrentUrl.empty()) {
        dlg->setTorrentUrl(prefilledTorrentUrl);
    }
    if (!prefilledBody.empty()) {
        dlg->setTorrentBody(prefilledBody);
    }
    if (!prefilledMagnet.empty()) {
        dlg->setMagnetLink(prefilledMagnet);
        // A .torrent file fills the list itself; a magnet cannot, so the
        // search row's already-resolved list is what the checkboxes show.
        // Empty (never expanded / fetch failed) just means "download all".
        if (prefilledPath.empty() && !prefilledFiles.empty()) {
            dlg->setFileList(prefilledFiles);
        }
    }

    bool added = false;
    if (dlg->show_modal() && m_manager) {
        std::string path = dlg->getTorrentPath();
        std::string magnet = dlg->getMagnetLink();
        std::string savePath = dlg->getSavePath();
        auto filePriorities = dlg->getFilePriorities();

        bool success = false;
        if (!path.empty()) {
            success = m_manager->addTorrentFile(path, savePath, filePriorities);
        } else if (!magnet.empty()) {
            // Honour the checkboxes only when they describe this exact magnet:
            // the list was resolved for prefilledMagnet, so an edited field or
            // a length mismatch falls back to "download all" rather than
            // shifting libtorrent's file order. The priority vector is indexed
            // by libtorrent file index, so pad files count even though they
            // have no row in the dialog.
            int expectedFiles = static_cast<int>(prefilledFiles.size());
            for (const TorrentFileEntry& file : prefilledFiles) {
                expectedFiles = std::max(expectedFiles, file.fileIndex + 1);
            }
            if (magnet != prefilledMagnet) {
                filePriorities.clear();
            } else if (filePriorities.size() !=
                       static_cast<std::size_t>(expectedFiles)) {
                filePriorities.clear();
            }
            success = m_manager->addMagnetLink(magnet, savePath, filePriorities);
        }

        if (success) {
            updateUI();
            added = true;
        } else {
            fl_alert("Failed to add torrent");
        }
    }

    delete dlg;
    return added;
}

void MainWindow::showCreateTorrentDialog() {
    CreateTorrentDialog* dlg = new CreateTorrentDialog();
    dlg->show_modal();
    delete dlg;
}

void MainWindow::showPreferencesDialog() {
    PreferencesDialog* dlg = new PreferencesDialog();
    
    if (dlg->show_modal()) {
        // Settings were saved, might need to apply some changes
        auto& settings = SettingsManager::instance();
        m_censored = settings.getIpCensored();
        m_showPublicIp = settings.getShowPublicIp();
        updateCensorButtonState();
        updateStatusBar();
        updateUI();
    }
    
    delete dlg;
}

void MainWindow::showAboutDialog() {
    std::string message = "FTorrent v";
    message += VERSION;
    message += "\n\n"
               "A lightweight BitTorrent client built with FLTK\n\n"
               "Powered by libtorrent-rasterbar\n\n"
               "© 2026";
    fl_message("%s", message.c_str());
}

void MainWindow::toggleSelectedTorrents() {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    if (selected.empty()) return;

    // Determine action based on the *first* selected item
    bool shouldResume = (selected[0]->getState() == TorrentItem::State::Paused);

    for (auto* torrent : selected) {
        if (shouldResume) {
            m_manager->resumeTorrent(torrent->getHash());
        } else {
            m_manager->pauseTorrent(torrent->getHash());
        }
    }
    
    updateUI();
}

void MainWindow::removeSelectedTorrents() {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    if (selected.empty()) return;
    
    RemoveConfirmDialog* dlg = new RemoveConfirmDialog(selected.size());
    int result = dlg->show_modal();
    bool shouldDeleteFiles = dlg->shouldDeleteFiles();
    delete dlg;
    
    if (result == 1) { // Removed
        // Collect hashes first to avoid dangling pointers if removals trigger list updates
        std::vector<std::string> hashes;
        for (auto* torrent : selected) {
            if (torrent) {
                hashes.push_back(torrent->getHash());
            }
        }
        
        for (const auto& hash : hashes) {
            m_manager->removeTorrent(hash, shouldDeleteFiles);
        }
    }
}
