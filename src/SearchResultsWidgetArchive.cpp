#include "SearchResultsWidget.h"

#include "ArchiveCheckTask.h"
#include "ArchiveInspector.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

int SearchResultsWidget::archiveIndexFor(
    const std::vector<TorrentFileEntry>& files) const {
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (ArchiveInspector::canListBeforeDownload(files[i].path)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int SearchResultsWidget::encryptedForRow(int row) {
    const SearchResult* r = getResultAt(row);
    if (!r) return 0;
    // find(), not operator[]: drawing must not create an entry for a row whose
    // archive was never checked.
    auto action = m_archive.find(cacheKey(*r));
    return action == m_archive.end() ? 0 : action->second.encrypted;
}

int SearchResultsWidget::archiveEntryRows(const ArchiveAction& action) {
    if (action.running || action.line.empty() || action.entries.empty()) {
        return 0;
    }
    const int shown = static_cast<int>(action.entries.size());
    const int listed = shown > MAX_SHOWN_ARCHIVE_ENTRIES
                           ? MAX_SHOWN_ARCHIVE_ENTRIES
                           : shown;
    return listed + (shown > listed ? 1 : 0);   // +1 for "... and N more"
}

bool SearchResultsWidget::archiveLineHit(int row, int x, int y) {
    const SearchResult* result = getResultAt(row);
    if (!result) return false;
    auto action = m_archive.find(cacheKey(*result));
    if (action == m_archive.end() || !action->second.visible) return false;

    int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
    if (!find_cell(CONTEXT_CELL, row, COL_NAME, cellX, cellY, cellW, cellH)) {
        return false;
    }
    (void)cellY;
    (void)cellH;
    if (x < cellX || x > cellX + cellW) return false;
    return y >= action->second.top && y <= action->second.bottom;
}

void SearchResultsWidget::startArchiveCheck(const std::string& key) {
    auto action = m_archive.find(key);
    if (action == m_archive.end() || action->second.running) return;

    ArchiveAction& entry = action->second;
    if (entry.magnet.empty() && entry.torrentUrl.empty()) {
        entry.line =
            "Archive: this result carries neither a .torrent nor a magnet.";
        redraw();
        return;
    }

    entry.running = true;
    entry.line.clear();
    // A new check answers the old question: the previous entries go with it.
    entry.encrypted = 0;
    entry.entries.clear();

    const int index = entry.fileIndex;
    const std::string path = entry.path;
    const std::string magnet = entry.magnet;
    const std::string torrentUrl = entry.torrentUrl;
    // The file list the row already resolved carries the .torrent bytes, so
    // the check can go straight to the index instead of repeating the metadata
    // exchange with the swarm.
    const FileListService::Entry known = m_files.find(key);
    const std::string torrentBody = known.state == FileListState::Loaded
                                        ? known.torrentBody
                                        : std::string();
    std::shared_ptr<bool> alive = m_alive;
    SearchResultsWidget* self = this;

    // One task owns the worker thread, the cancellation and the hop back to the
    // UI thread; this widget only decides where the answer goes.
    auto deliver = [self, alive, key](ArchiveCheckTask::Result result) {
        if (!alive || !*alive) return;
        auto found = self->m_archive.find(key);
        if (found != self->m_archive.end()) {
            found->second.running = false;
            found->second.line = result.message;
            found->second.encrypted = result.encrypted;
            found->second.entries = std::move(result.entries);
        }
        self->forEachRow([&](int row, const SearchResult& r) {
            if (SearchResultsWidget::cacheKey(r) == key) {
                self->updateRowHeight(row);
            }
        });
        self->redraw();
    };

    if (!torrentBody.empty()) {
        // A check already running for this row is stopped first: the row only
        // shows one answer, and the older one would arrive late and overwrite
        // it.
        if (m_archiveTasks[key]) m_archiveTasks[key]->cancel();
        m_archiveTasks[key] = ArchiveCheckTask::startFromTorrentBody(
            torrentBody, index, path, deliver);
    } else {
        if (m_archiveTasks[key]) m_archiveTasks[key]->cancel();
        m_archiveTasks[key] = ArchiveCheckTask::startFromMagnet(
            magnet, torrentUrl, index, path, deliver);
    }

    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
}
