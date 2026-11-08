#include "AddTorrentDialogArchive.h"
#include "AddTorrentDialog.h"
#include "AddTorrentDialogReputation.h"

#include "ArchiveCheckTask.h"
#include "ArchiveInspector.h"
#include "SecurityBadge.h"
#include "TorrentFileList.h"

#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/load_torrent.hpp>

AddTorrentDialogArchive::AddTorrentDialogArchive(AddTorrentDialog* dlg)
    : dlg(dlg) {}

const TorrentFileEntry* AddTorrentDialogArchive::target() const {
    if (!dlg->m_fileBrowser) return nullptr;

    const int highlighted = dlg->m_fileBrowser->value();
    if (highlighted >= 1 &&
        highlighted <= static_cast<int>(dlg->m_files.size())) {
        const TorrentFileEntry& file =
            dlg->m_files[static_cast<std::size_t>(highlighted - 1)];
        if (ArchiveInspector::canListBeforeDownload(file.path)) return &file;
    }

    for (int row = 0; row < static_cast<int>(dlg->m_files.size()); ++row) {
        if (!dlg->m_fileBrowser->checked(row + 1)) continue;
        const TorrentFileEntry& file = dlg->m_files[static_cast<std::size_t>(row)];
        if (ArchiveInspector::canListBeforeDownload(file.path)) return &file;
    }

    for (const TorrentFileEntry& file : dlg->m_files) {
        if (ArchiveInspector::canListBeforeDownload(file.path)) return &file;
    }
    return nullptr;
}

void AddTorrentDialogArchive::checkArchive() {
    if (dlg->m_archiveRunning) return;

    const TorrentFileEntry* archive = target();
    if (!archive) {
        dlg->m_checkLine = "Archive: this torrent has no .zip or .rar to list.";
        dlg->m_reputationHelper->refreshReputationLabel();
        return;
    }

    const std::string torrentPath = dlg->m_filePathInput &&
                                            dlg->m_filePathInput->value()
                                        ? dlg->m_filePathInput->value()
                                        : "";
    const std::string magnet =
        dlg->m_magnetInput && dlg->m_magnetInput->value()
            ? dlg->m_magnetInput->value()
            : "";
    std::string body = dlg->m_torrentBody;
    if (!body.empty() && !magnet.empty()) {
        const std::string expected = TorrentFileList::magnetHash(magnet);
        const std::string actual = TorrentFileList::infoHashOfTorrentFile(body);
        if (!expected.empty() && !actual.empty() && expected != actual) {
            body.clear();
        }
    }
    const std::string torrentUrl = dlg->m_torrentUrl;
    if (torrentPath.empty() && body.empty() && torrentUrl.empty() &&
        magnet.empty()) {
        dlg->m_checkLine = "Archive: open a .torrent file or a magnet first.";
        dlg->m_reputationHelper->refreshReputationLabel();
        return;
    }

    dlg->m_archiveRunning = true;
    dlg->m_checkArchive->deactivate();
    dlg->m_checkLine = "Archive: reading the archive index...";
    dlg->m_reputationHelper->refreshReputationLabel();
    dlg->m_archiveEntries.clear();
    if (dlg->m_archiveBrowser) {
        dlg->m_archiveBrowser->clear();
        dlg->m_archiveBrowser->hide();
    }

    const int index = archive->fileIndex;
    const std::string path = archive->path;
    std::shared_ptr<bool> alive = dlg->m_alive;
    AddTorrentDialog* self = dlg;

    auto deliver = [self, alive](ArchiveCheckTask::Result result) {
        if (!alive || !*alive) return;
        self->m_archiveRunning = false;
        self->m_checkLine = result.message;
        self->m_archiveEntries = std::move(result.entries);
        self->m_reputationHelper->refreshReputationLabel();
        self->updateSecurityPanel();
        if (self->m_archiveHelper) self->m_archiveHelper->showEntries();
    };

    if (!body.empty()) {
        dlg->m_archiveTask =
            ArchiveCheckTask::startFromTorrentBody(body, index, path, deliver);
    } else {
        dlg->m_archiveTask = ArchiveCheckTask::startFromMagnet(
            magnet, torrentPath.empty() ? torrentUrl : torrentPath, index, path,
            deliver);
    }
}

void AddTorrentDialogArchive::showEntries() {
    if (!dlg->m_archiveBrowser) return;
    dlg->m_archiveBrowser->clear();
    if (dlg->m_archiveEntries.empty()) {
        dlg->m_archiveBrowser->hide();
        return;
    }
    for (const SecurityFile& entry : dlg->m_archiveEntries) {
        dlg->m_archiveBrowser->add(
            SecurityBadge::archiveEntryLine(entry).c_str());
    }
    dlg->m_archiveBrowser->show();
    dlg->redraw();
}

void AddTorrentDialogArchive::onCheckArchive(Fl_Widget*, void* data) {
    auto* self = static_cast<AddTorrentDialogArchive*>(data);
    if (self) self->checkArchive();
}
