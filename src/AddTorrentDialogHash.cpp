#include "AddTorrentDialogHash.h"
#include "AddTorrentDialog.h"
#include "AddTorrentDialogReputation.h"
#include "TorrentFileHashTask.h"

#include <FL/fl_ask.H>

#include <cstdlib>
#include <filesystem>

AddTorrentDialogFileHash::AddTorrentDialogFileHash(AddTorrentDialog* dlg)
    : dlg(dlg) {}

bool AddTorrentDialogFileHash::createQuarantineDir(std::string& error) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string name = "ftorrent-quarantine-";
    for (int i = 0; i < 12; ++i)
        name += charset[static_cast<std::size_t>(std::rand()) %
                       (sizeof(charset) - 1)];

    m_quarantineDir =
        std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(m_quarantineDir, ec);
    if (!std::filesystem::create_directories(m_quarantineDir, ec)) {
        error = "could not create quarantine directory: " + ec.message();
        return false;
    }

#if !defined(_WIN32)
    std::filesystem::permissions(
        m_quarantineDir,
        std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write |
        std::filesystem::perms::group_read |
        std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);
#endif
    return true;
}

void AddTorrentDialogFileHash::cleanupQuarantine() {
    if (!m_quarantineDir.empty()) {
        std::error_code ec;
        for (auto& p : std::filesystem::recursive_directory_iterator(
                 m_quarantineDir, ec)) {
            if (!ec) std::filesystem::remove_all(p.path(), ec);
        }
        std::filesystem::remove_all(m_quarantineDir, ec);
        m_quarantineDir.clear();
    }
}

bool AddTorrentDialogFileHash::calculateHash() {
    if (dlg->m_fileHashRunning || dlg->m_archiveRunning) return false;

    const TorrentFileEntry* target = nullptr;
    const int highlighted =
        dlg->m_fileBrowser ? dlg->m_fileBrowser->value() : 0;
    if (highlighted >= 1 &&
        highlighted <= static_cast<int>(dlg->m_files.size())) {
        target = &dlg->m_files[static_cast<std::size_t>(highlighted - 1)];
    }
    if (!target) {
        for (int row = 0; row < static_cast<int>(dlg->m_files.size()); ++row) {
            if (dlg->m_fileBrowser->checked(row + 1)) {
                target = &dlg->m_files[static_cast<std::size_t>(row)];
                break;
            }
        }
    }
    if (!target && !dlg->m_files.empty()) target = &dlg->m_files.front();
    if (!target) {
        dlg->m_checkLine = "Hash: no file to hash.";
        dlg->m_reputationHelper->refreshReputationLabel();
        return false;
    }

    if (target->sha256Complete && !target->sha256.empty()) {
        dlg->m_fileHashSha256 = target->sha256;
        dlg->m_fileHashPath = target->path;
        dlg->m_checkLine = "Hash: " + target->sha256;
        dlg->m_reputationHelper->refreshReputationLabel();
        return true;
    }

    if (!m_hashQuarantine && !m_hashQuarantineAsked) {
        m_hashQuarantineAsked = true;
        const int choice =
            fl_choice("Hashing %s needs to download its pieces to compute "
                      "the SHA-256. Use a quarantined temp folder?\n"
                      "(isolated, restrictive permissions, deleted after)\n\n"
                      "No = regular temp folder, also deleted after.",
                      "Yes, quarantine", "No, regular temp", nullptr,
                      target->path.c_str());
        m_hashQuarantine = (choice == 0);
    }

    std::string quarantineError;
    std::string quarantineDir;
    if (m_hashQuarantine) {
        if (!createQuarantineDir(quarantineError)) {
            dlg->m_checkLine = "Hash: " + quarantineError;
            dlg->m_reputationHelper->refreshReputationLabel();
            m_hashQuarantine = false;
            return false;
        }
        quarantineDir = m_quarantineDir.string();
    }

    dlg->m_fileHashRunning = true;
    dlg->m_calculateHashButton->deactivate();
    dlg->m_checkLine = "Hash: reading " + target->path + " pieces...";
    dlg->m_reputationHelper->refreshReputationLabel();

    dlg->m_fileHashSha256.clear();
    dlg->m_fileHashPath = target->path;

    auto params = dlg->buildAddTorrentParams();
    if (!params) {
        dlg->m_fileHashRunning = false;
        dlg->m_calculateHashButton->activate();
        dlg->m_checkLine = "Hash: could not build torrent parameters.";
        dlg->m_reputationHelper->refreshReputationLabel();
        cleanupQuarantine();
        return false;
    }

    std::shared_ptr<bool> alive = dlg->m_alive;
    AddTorrentDialog* self = dlg;
    auto deliver = [self, alive](TorrentFileHashTask::Result result) {
        if (!alive || !*alive) return;
        self->m_fileHashRunning = false;
        self->m_calculateHashButton->activate();
        if (result.ok) {
            self->m_fileHashSha256 = result.sha256;
            self->m_checkLine = "Hash: " + result.sha256;
        } else {
            self->m_checkLine = "Hash: " + result.error;
        }
        self->m_reputationHelper->refreshReputationLabel();
        self->updateSecurityPanel();
        if (self->m_fileHashHelper) self->m_fileHashHelper->cleanupQuarantine();
    };

    dlg->m_fileHashTask = TorrentFileHashTask::start(
        *params, target->fileIndex, target->path, dlg->m_savePath,
        deliver, quarantineDir);
    return true;
}

void AddTorrentDialogFileHash::onCalculateHash(Fl_Widget*, void* data) {
    auto* self = static_cast<AddTorrentDialogFileHash*>(data);
    if (self) self->calculateHash();
}
