#include "AddTorrentDialog.h"
#include "AddTorrentDialogReputation.h"

#include "ReputationService.h"
#include "SecurityReputation.h"
#include "TorrentFileHashTask.h"

#include <FL/Fl.H>

#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>

#include <algorithm>
#include <filesystem>

AddTorrentDialogReputation::AddTorrentDialogReputation(AddTorrentDialog* dlg)
    : dlg(dlg) {}

ReputationTarget AddTorrentDialogReputation::prepareReputation(
    const std::vector<TorrentFileEntry>& selected) {
    const ReputationTarget target = findReputationTarget(selected);

    if (target.found()) return target;

    // No whole-file hash in the metadata. Look for a runnable selected file
    // that is not yet on disk: we can hash just its pieces without downloading
    // the whole torrent.
    const TorrentFileEntry* hashTarget = nullptr;
    for (const auto& file : selected) {
        if (file.executable && !file.sha256Complete) {
            hashTarget = &file;
            break;
        }
    }

    if (!hashTarget) return ReputationTarget{};

    // Calculate the hash of this executable from its pieces alone. The user
    // clicked the shield: that is the explicit request. We never download the
    // whole file, only the pieces that cover it.
    dlg->m_fileHashSha256.clear();
    dlg->m_fileHashPath = hashTarget->path;
    dlg->m_fileHashRunning = true;
    dlg->m_reputation = ReputationResult{};
    dlg->m_reputation.state = ReputationState::Querying;
    dlg->m_checkLine = "Hash: reading the file's pieces...";
    refreshReputationLabel();

    auto params = buildAddTorrentParams();
    if (!params) {
        dlg->m_fileHashRunning = false;
        dlg->m_reputation = ReputationService::noHashResult();
        refreshReputationLabel();
        return ReputationTarget{};
    }

    std::shared_ptr<bool> alive = dlg->m_alive;
    dlg->m_fileHashTask = TorrentFileHashTask::start(
        *params, hashTarget->fileIndex, hashTarget->path, dlg->m_savePath,
        [this, alive](TorrentFileHashTask::Result result) {
            if (!alive || !*alive) return;
            dlg->m_fileHashRunning = false;
            dlg->m_fileHashTask.reset();
            if (result.ok) {
                dlg->m_fileHashSha256 = result.sha256;
                dlg->m_reputationSha = result.sha256;
                dlg->m_checkLine = "Hash: " + result.sha256;
            } else {
                dlg->m_checkLine = "Hash: " + result.error;
            }
            this->refreshReputationLabel();
        });

    return ReputationTarget{};
}

void AddTorrentDialogReputation::refreshReputationLabel() {
    if (!dlg->m_reputationLabel) return;

    // Whichever explicit check ran last owns the line. Both say what they found
    // and neither calls anything safe. The small print follows: which hash the
    // metadata carries, so the user can check it themselves, or that there is
    // none to check.
    std::string line =
        dlg->m_checkLine.empty()
            ? reputationLine(dlg->m_reputation)
            : dlg->m_checkLine;
    const std::string hash =
        !dlg->m_fileHashSha256.empty()   ? dlg->m_fileHashSha256 :
        !dlg->m_reputation.sha256.empty() ? dlg->m_reputation.sha256
                                         : dlg->m_torrentSha256;
    const std::string hashPath =
        !dlg->m_fileHashSha256.empty()   ? dlg->m_fileHashPath :
        !dlg->m_reputation.sha256.empty() ? std::string()
                                          : dlg->m_torrentSha256Path;
    line += "\n";
    line += hashSmallPrint(hash, hashPath);
    dlg->m_reputationLabel->copy_label(line.c_str());
    if (reputationHasDetections(dlg->m_reputation)) {
        dlg->m_reputationLabel->labelcolor(
            SecurityBadge::riskColor(RiskLevel::Critical));
    } else if (dlg->m_reputation.state == ReputationState::Querying ||
               dlg->m_archiveRunning || dlg->m_fileHashRunning) {
        dlg->m_reputationLabel->labelcolor(
            SecurityBadge::riskColor(RiskLevel::Medium));
    } else {
        dlg->m_reputationLabel->labelcolor(fl_rgb_color(110, 110, 110));
    }
    dlg->m_reputationLabel->redraw();
}

std::optional<lt::add_torrent_params>
AddTorrentDialogReputation::buildAddTorrentParams() {
    const std::string torrentPath = dlg->m_filePathInput &&
                                            dlg->m_filePathInput->value()
                                        ? dlg->m_filePathInput->value()
                                        : "";
    const std::string magnet =
        dlg->m_magnetInput && dlg->m_magnetInput->value()
            ? dlg->m_magnetInput->value()
            : "";
    std::string body = dlg->m_torrentBody;
    // A body that describes a different torrent than the magnet on screen must
    // never be used: the answer would be about someone else's files.
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
        return std::nullopt;
    }

    lt::add_torrent_params atp;
    if (!body.empty()) {
        try {
            atp = lt::load_torrent_buffer(
                lt::span<char const>(body.data(), body.size()));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    } else if (!torrentPath.empty()) {
        try {
            atp = lt::load_torrent_file(torrentPath);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    } else {
        lt::error_code mec;
        atp = lt::parse_magnet_uri(magnet, mec);
        if (mec) return std::nullopt;
        TorrentFileList::addDefaultTrackers(atp.trackers);
    }
    return atp;
}
