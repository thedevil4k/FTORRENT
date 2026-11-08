#ifndef ADDTORRENTDIALOG_HASH_H
#define ADDTORRENTDIALOG_HASH_H

#include <FL/Fl.H>
#include <filesystem>
#include <string>

class AddTorrentDialog;

/**
 * @brief On-demand, per-file SHA-256 from torrent pieces.
 *
 * Hashes only the pieces that cover one selected file, never the whole
 * torrent. Opt-in: the user clicks a button for the highlighted file. An
 * optional quarantined temp dir isolates the downloaded pieces while they are
 * read. Nothing is executed and nothing is uploaded.
 */
class AddTorrentDialogFileHash {
public:
    explicit AddTorrentDialogFileHash(AddTorrentDialog* dlg);

    // Hash the highlighted (or first checked, or first) file's pieces.
    bool calculateHash();

    bool createQuarantineDir(std::string& error);
    void cleanupQuarantine();

    static void onCalculateHash(Fl_Widget* w, void* data);

private:
    AddTorrentDialog* dlg;
    bool m_hashQuarantine = false;
    bool m_hashQuarantineAsked = false;
    std::filesystem::path m_quarantineDir;
};

#endif // ADDTORRENTDIALOG_HASH_H
