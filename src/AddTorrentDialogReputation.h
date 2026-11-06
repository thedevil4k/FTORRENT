#ifndef ADDTORRETDIALOGREPUTATION_H
#define ADDTORRETDIALOGREPUTATION_H

#include <libtorrent/add_torrent_params.hpp>
#include <memory>
#include <optional>
#include <string>

class AddTorrentDialog;

/**
 * @brief Reputation logic for AddTorrentDialog.
 *
 * Kept in a separate file so AddTorrentDialog.cpp stays under the source-size
 * limit. Has friend access to AddTorrentDialog's private members. Does NOT call
 * ReputationService::request directly — that stays in AddTorrentDialog so the
 * invariant checker sees the reputation action owned by AddTorrentDialog.
 */
class AddTorrentDialogReputation {
public:
    explicit AddTorrentDialogReputation(AddTorrentDialog* dlg);

    // Prepares the reputation target from the selected files. If no whole-file
    // hash exists in the metadata, looks for a runnable file to hash from its
    // pieces and starts the piece-fetching task. Returns the target (empty if
    // nothing can be asked about).
    ReputationTarget prepareReputation(const std::vector<TorrentFileEntry>& selected);

    void refreshReputationLabel();
    std::optional<lt::add_torrent_params> buildAddTorrentParams();

private:
    AddTorrentDialog* dlg;
};

#endif // ADDTORRETDIALOGREPUTATION_H
