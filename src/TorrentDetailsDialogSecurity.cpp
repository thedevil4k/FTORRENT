#include "TorrentDetailsDialog.h"
#include "ArchiveCheckTask.h"
#include "ArchiveInspector.h"
#include "FileHash.h"
#include "ReputationService.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"
#include "SecurityReputation.h"
#include "SecurityTypes.h"
#include <FL/Fl.H>
#include <atomic>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

void TorrentDetailsDialog::createSecurityTab() {
    m_securityTab = new Fl_Group(10, 35, 580, 405, "Security");
    m_securityTab->hide();
    m_securityTab->begin();

    m_securityShield = new SecurityShieldBox(20, 50, 24, 24);
    m_securityShield->setRisk(
        RiskLevel::Unknown,
        "No file list available. Metadata risk is unknown.");
    m_securityShield->onClick([this]() { checkReputation(); });

    m_securitySummary = new Fl_Box(54, 50, 380, 24);
    m_securitySummary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_securitySummary->labelfont(FL_HELVETICA_BOLD);

    // Level 2 lives here: the answer needs room, and this tab is where the
    // user is already reading about the torrent.
    m_checkArchiveButton = new Fl_Button(440, 48, 140, 26, "Check archive");
    m_checkArchiveButton->callback(onCheckArchiveClicked, this);
    m_checkArchiveButton->tooltip(
        "Reads only the index of a RAR or ZIP (two pieces for ZIP/RAR5, a "
        "bounded header walk for older RAR) to list what is "
        "inside. The pieces go to a temporary folder and are deleted.");
    m_checkArchiveButton->deactivate();

    m_securityBuffer = new Fl_Text_Buffer();
    m_securityDisplay = new Fl_Text_Display(20, 84, 560, 346);
    m_securityDisplay->buffer(m_securityBuffer);
    m_securityDisplay->textfont(FL_COURIER);
    m_securityDisplay->textsize(11);
    m_securityDisplay->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    m_securityTab->end();
}
void TorrentDetailsDialog::updateSecurity() {
    if (!m_securityShield || !m_securitySummary || !m_securityBuffer) return;

    // The file list of a torrent does not change, so one successful analysis
    // is final. While a magnet is still fetching metadata, retry on the next
    // two-second tick instead of caching "unknown" forever. The reputation
    // section below is rebuilt every time, because it changes on its own.
    if (!m_securityLoaded) {
        const std::vector<SecurityFile> files = m_torrent->getSecurityFiles();
        m_securityReport = SecurityAnalyzer::analyze(files);
        if (!files.empty()) m_securityLoaded = true;

        // A different set of hashes means the previous answer is stale.
        const ReputationTarget target = findReputationTarget(files);
        if (target.sha256 != m_reputationSha) {
            m_reputationSha = target.sha256;
            m_reputation = ReputationResult{};
        }
    }

    const RiskLevel level = m_securityReport.level;
    const Fl_Color color = SecurityBadge::riskColor(level);
    const std::string summary =
        SecurityBadge::riskLabel(level) + "  -  " + m_securityReport.summary;
    const std::string shown = SecurityBadge::displayText(summary);

    // The tooltip carries the metadata verdict and, once the user asked, the
    // reputation answer. The shield is what starts that request.
    const std::string tooltip =
        shown + "\n" + SecurityBadge::displayText(reputationLine(m_reputation));
    m_generalShield->setRisk(level, tooltip);
    m_securityShield->setRisk(level, tooltip);
    m_securitySummary->copy_label(shown.c_str());
    m_securitySummary->labelcolor(color);
    if (m_generalSecuritySummary) {
        m_generalSecuritySummary->copy_label(shown.c_str());
        m_generalSecuritySummary->labelcolor(color);
    }

    std::ostringstream out;
    out << "Metadata risk: " << SecurityBadge::riskLabel(level) << "\n\n";

    if (!m_securityReport.hasFileList()) {
        out << "No file list is available yet. For a magnet link the metadata\n"
               "arrives with the torrent; this tab updates automatically when\n"
               "it does.\n";
    } else {
        out << "Files: " << m_securityReport.files
            << "  |  Executables: " << m_securityReport.executables
            << "  |  Scripts: " << m_securityReport.scripts
            << "  |  Archives: " << m_securityReport.archives << "\n";
        if (m_securityReport.shortcuts > 0) {
            out << "Shortcuts: " << m_securityReport.shortcuts << "\n";
        }
        if (m_securityReport.symlinks > 0) {
            out << "Symlinks: " << m_securityReport.symlinks << "\n";
        }
        out << "\nNo file contents were read. A name cannot prove what a file\n"
               "does: \"no known indicators\" is not a guarantee.\n";

        if (m_securityReport.findings.empty()) {
            out << "\nNo known indicators in file names or metadata.\n";
        } else {
            out << "\nFindings:\n";
            for (const Finding& finding : m_securityReport.findings) {
                out << "  ["
                    << SecurityAnalyzer::severityLabel(finding.severity) << "] "
                    << SecurityBadge::displayText(
                           SecurityAnalyzer::describeFinding(finding))
                    << "\n";
            }
        }
    }

    out << "\nReputation\n  "
        << SecurityBadge::displayText(
               m_hashingLocally
                   ? "Computing the SHA-256 of the completed file on this "
                     "machine... nothing is uploaded."
                   : reputationLine(m_reputation))
        << "\n";
    // The small print: the hash itself, or an honest "none". It is shown even
    // when no provider was asked, because knowing which hash was used is not
    // the same question as what a provider said about it.
    {
        const std::string hash = m_reputation.sha256.empty()
                                     ? m_securityReport.sha256
                                     : m_reputation.sha256;
        const std::string path = m_reputation.sha256.empty()
                                     ? m_securityReport.sha256Path
                                     : std::string();
        out << "  " << SecurityBadge::displayText(hashSmallPrint(hash, path))
            << "\n";
    }
    if (!m_reputationNote.empty()) {
        out << "  " << SecurityBadge::displayText(m_reputationNote) << "\n";
    }

    if (m_archiveRunning || !m_archiveLine.empty()) {
        out << "\nArchive\n  "
            << SecurityBadge::displayText(m_archiveRunning
                                              ? "Reading the archive index..."
                                              : m_archiveLine)
            << "\n";
        // The entries the index lists, one per line. Names and sizes only:
        // nothing inside the archive is read. The list is long, so it goes at
        // the end of this buffer where the user can scroll it.
        if (!m_archiveRunning && !m_archiveEntries.empty()) {
            out << "\n";
            const std::size_t cap = 200;
            const std::size_t listed =
                m_archiveEntries.size() > cap ? cap : m_archiveEntries.size();
            for (std::size_t i = 0; i < listed; ++i) {
                out << "  "
                    << SecurityBadge::displayText(
                           SecurityBadge::archiveEntryLine(m_archiveEntries[i]))
                    << "\n";
            }
            if (m_archiveEntries.size() > listed) {
                out << "  ... and " << (m_archiveEntries.size() - listed)
                    << " more\n";
            }
        }
    }

    m_securityBuffer->text(out.str().c_str());

    // The archive button only makes sense when there is one to read.
    if (m_checkArchiveButton) {
        bool hasArchive = false;
        if (const std::shared_ptr<const lt::torrent_info> info =
                m_torrent->getHandle().torrent_file()) {
            const lt::file_storage& storage =
#if LIBTORRENT_VERSION_NUM >= 20100
                info->layout();
#else
                info->files();
#endif
            for (int i = 0; i < storage.num_files() && !hasArchive; ++i) {
                if (storage.pad_file_at(lt::file_index_t(i))) continue;
                hasArchive = ArchiveInspector::canListBeforeDownload(
                    std::string(storage.file_path(lt::file_index_t(i))));
            }
        }
        if (hasArchive && !m_archiveRunning) m_checkArchiveButton->activate();
        else m_checkArchiveButton->deactivate();
    }
}
void TorrentDetailsDialog::onCheckArchiveClicked(Fl_Widget*, void* data) {
    TorrentDetailsDialog* dlg = static_cast<TorrentDetailsDialog*>(data);
    if (dlg) dlg->checkArchive();
}
void TorrentDetailsDialog::checkArchive() {
    if (m_archiveRunning || !m_torrent) return;

    // The first archive in the torrent, so the answer names one file.
    const std::vector<TorrentItem::FileInfo> files = m_torrent->getFiles();
    const TorrentItem::FileInfo* archive = nullptr;
    for (const TorrentItem::FileInfo& file : files) {
        if (ArchiveInspector::canListBeforeDownload(file.path)) {
            archive = &file;
            break;
        }
    }
    if (!archive) {
        m_archiveLine = "Archive: this torrent holds no RAR or ZIP.";
        m_archiveEntries.clear();
        updateSecurity();
        return;
    }

    std::shared_ptr<const lt::torrent_info> info =
        m_torrent->getHandle().torrent_file();
    if (!info) {
        m_archiveLine =
            "Archive: the metadata is not here yet, so its pieces cannot be "
            "located.";
        m_archiveEntries.clear();
        updateSecurity();
        return;
    }

    // Handing the metadata over means no second exchange with the swarm. The
    // shared_ptr is only read from here on, so dropping const is safe.
    lt::add_torrent_params params;
    params.ti = std::const_pointer_cast<lt::torrent_info>(info);

    const int index = archive->fileIndex;
    const std::string path = archive->path;
    m_archiveRunning = true;
    m_archiveLine.clear();
    m_archiveEntries.clear();
    updateSecurity();

    std::shared_ptr<bool> alive = m_alive;
    TorrentDetailsDialog* self = this;

    // One task owns the worker thread, the cancellation and the hop back to the
    // UI thread; this dialog only says what to check and what to do with the
    // answer.
    auto deliver = [self, alive](ArchiveCheckTask::Result result) {
        if (!alive || !*alive) return;
        self->m_archiveRunning = false;
        self->m_archiveLine = result.message;
        self->m_archiveEntries = std::move(result.entries);
        self->updateSecurity();
    };

    m_archiveTask = ArchiveCheckTask::startFromParams(std::move(params), index,
                                                      path, deliver);
}
void TorrentDetailsDialog::checkReputation() {
    const std::vector<SecurityFile> files = m_torrent->getSecurityFiles();
    const ReputationTarget target = findReputationTarget(files);

    if (target.found()) {
        m_reputationNote.clear();
        m_reputationSha = target.sha256;
        requestReputation(target.sha256);
        return;
    }

    // The metadata carries no whole-file hash. If a file that can run is
    // already complete on disk, hash it here and ask about that digest. This
    // never downloads anything and never uploads anything.
    std::vector<bool> complete;
    for (const TorrentItem::FileInfo& info : m_torrent->getFiles()) {
        complete.push_back(info.complete);
    }
    const ReputationTarget local = findLocalHashTarget(files, complete);
    if (local.path.empty()) {
        m_reputationSha.clear();
        m_reputationNote.clear();
        m_reputation = ReputationService::noHashResult();
        updateSecurity();
        return;
    }
    startLocalHash(local.path);
}
void TorrentDetailsDialog::startLocalHash(const std::string& relativePath) {
    if (m_hashingLocally || !m_torrent) return;

    const std::filesystem::path disk =
        std::filesystem::path(m_torrent->getSavePath()) /
        std::filesystem::path(relativePath);
    const std::string diskPath = disk.string();

    m_hashingLocally = true;
    m_reputationSha.clear();
    m_reputation = ReputationResult{};
    m_reputationNote.clear();
    updateSecurity();

    struct Delivery {
        TorrentDetailsDialog* dialog = nullptr;
        std::shared_ptr<bool> alive;
        std::string relativePath;
        bool ok = false;
        std::string digestOrError;
    };

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_hashCancel = cancel;
    std::shared_ptr<bool> alive = m_alive;
    TorrentDetailsDialog* self = this;

    std::thread([self, alive, cancel, relativePath, diskPath]() {
        std::string digest;
        std::string error;
        const bool ok =
            FileHash::sha256File(diskPath, digest, error, cancel.get());

        Delivery* delivery = new Delivery;
        delivery->dialog = self;
        delivery->alive = alive;
        delivery->relativePath = relativePath;
        delivery->ok = ok;
        delivery->digestOrError = ok ? digest : error;
        Fl::awake(
            [](void* data) {
                std::unique_ptr<Delivery> delivery(
                    static_cast<Delivery*>(data));
                if (!delivery->alive || !*delivery->alive) return;
                delivery->dialog->onLocalHashDone(delivery->relativePath,
                                                  delivery->ok,
                                                  delivery->digestOrError);
            },
            delivery);
    }).detach();
}
void TorrentDetailsDialog::onLocalHashDone(const std::string& relativePath,
                                           bool ok,
                                           const std::string& digestOrError) {
    m_hashingLocally = false;
    if (!ok) {
        m_reputation = ReputationResult{};
        m_reputation.state = ReputationState::Error;
        m_reputation.message =
            "The completed file could not be hashed: " + digestOrError;
        updateSecurity();
        return;
    }

    m_reputationNote = "SHA-256 computed locally from the completed file " +
                       relativePath + "; nothing was uploaded.";
    m_reputationSha = digestOrError;
    requestReputation(digestOrError);
}
void TorrentDetailsDialog::requestReputation(const std::string& sha256) {
    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    TorrentDetailsDialog* self = this;
    const ReputationService::Start start =
        service.request(sha256, [self, alive](const ReputationResult& result) {
            if (!alive || !*alive) return;
            self->m_reputation = result;
            self->updateSecurity();
        });

    m_reputationSha = sha256;
    switch (start) {
        case ReputationService::Start::Started:
        case ReputationService::Start::AlreadyRunning:
            m_reputation = ReputationResult{};
            m_reputation.state = ReputationState::Querying;
            m_reputation.sha256 = sha256;
            break;
        case ReputationService::Start::Cached:
            m_reputation = service.find(sha256);
            break;
        case ReputationService::Start::Rejected:
            m_reputation = ReputationResult{};
            m_reputation.state = ReputationState::Error;
            m_reputation.sha256 = sha256;
            m_reputation.message = "Too many reputation checks are running. "
                                   "Try again in a moment.";
            break;
    }
    updateSecurity();
}
