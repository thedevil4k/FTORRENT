#ifndef ADDTORRENTDIALOG_ARCHIVE_H
#define ADDTORRENTDIALOG_ARCHIVE_H

#include <FL/Fl.H>
#include "TorrentFileList.h"

class AddTorrentDialog;

/**
 * @brief Pre-download archive index check (RAR/ZIP only).
 *
 * Reads only the archive index from the swarm: two pieces for ZIP/RAR5, a
 * bounded header walk for older RAR. Never downloads the whole archive.
 */
class AddTorrentDialogArchive {
public:
    explicit AddTorrentDialogArchive(AddTorrentDialog* dlg);

    const TorrentFileEntry* target() const;
    void checkArchive();
    void showEntries();

    static void onCheckArchive(Fl_Widget* w, void* data);

private:
    AddTorrentDialog* dlg;
};

#endif // ADDTORRENTDIALOG_ARCHIVE_H
