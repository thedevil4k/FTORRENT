#include "TorrentDetailsDialog.h"
#include "ArchiveInspector.h"
#include "ArchivePreflight.h"
#include "FileHash.h"
#include "ReputationService.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"
#include "SecurityReputation.h"
#include "SecurityTypes.h"
#include <FL/Fl.H>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>

TorrentDetailsDialog::TorrentDetailsDialog(TorrentItem* torrent)
    : Fl_Window(600, 500, "Torrent Details")
    , m_torrent(torrent)
{
    if (!m_torrent) {
        return;
    }
    
    // Create tabs
    m_tabs = new Fl_Tabs(10, 10, 580, 430);
    
    createGeneralTab();
    createTrackersTab();
    createPeersTab();
    createSecurityTab();
    createFilesTab();
    
    m_tabs->end();
    
    createButtons();
    
    end();
    set_modal();
    
    // Initial update
    updateInfo();
    
    // Set up auto-update timer
    Fl::add_timeout(2.0, updateTimerCallback, this);
}

TorrentDetailsDialog::~TorrentDetailsDialog() {
    Fl::remove_timeout(updateTimerCallback, this);
    if (m_alive) *m_alive = false;
    // Stop a local hash, and a two-piece read, that are still running.
    if (m_hashCancel) m_hashCancel->store(true);
    if (m_archiveCancel) m_archiveCancel->store(true);
    
    delete m_trackersBuffer;
    delete m_peersBuffer;
    delete m_securityBuffer;
}

void TorrentDetailsDialog::createGeneralTab() {
    m_generalTab = new Fl_Group(10, 35, 580, 405, "General");
    m_generalTab->begin();
    
    int y = 50;
    int label_h = 20;
    int spacing = 25;
    
    // The shield sits next to the torrent name, matching the search row, the
    // Add Torrent dialog and the torrent list.
    m_generalShield = new SecurityShieldBox(20, y + 2, 16, 16);
    m_generalShield->setRisk(RiskLevel::Unknown,
        "No file list available. Metadata risk is unknown.");
    m_generalShield->onClick([this]() { checkReputation(); });

    m_nameLabel = new Fl_Box(42, y, 538, label_h);
    m_nameLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_sizeLabel = new Fl_Box(20, y, 560, label_h);
    m_sizeLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_statusLabel = new Fl_Box(20, y, 560, label_h);
    m_statusLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_downloadedLabel = new Fl_Box(20, y, 560, label_h);
    m_downloadedLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_uploadedLabel = new Fl_Box(20, y, 560, label_h);
    m_uploadedLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_ratioLabel = new Fl_Box(20, y, 560, label_h);
    m_ratioLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_downloadSpeedLabel = new Fl_Box(20, y, 560, label_h);
    m_downloadSpeedLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_uploadSpeedLabel = new Fl_Box(20, y, 560, label_h);
    m_uploadSpeedLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_peersLabel = new Fl_Box(20, y, 560, label_h);
    m_peersLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_seedsLabel = new Fl_Box(20, y, 560, label_h);
    m_seedsLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_etaLabel = new Fl_Box(20, y, 560, label_h);
    m_etaLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += spacing;
    
    m_savePathLabel = new Fl_Box(20, y, 560, label_h * 2);
    m_savePathLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    y += spacing * 2;
    
    m_hashLabel = new Fl_Box(20, y, 560, label_h);
    m_hashLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_hashLabel->labelsize(10);
    y += label_h;

    m_generalSecuritySummary = new Fl_Box(20, y, 560, 38);
    m_generalSecuritySummary->align(
        FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_generalSecuritySummary->labelsize(11);
    
    m_generalTab->end();
}

void TorrentDetailsDialog::createTrackersTab() {
    m_trackersTab = new Fl_Group(10, 35, 580, 405, "Trackers");
    m_trackersTab->hide();
    m_trackersTab->begin();
    
    m_trackersBuffer = new Fl_Text_Buffer();
    m_trackersDisplay = new Fl_Text_Display(20, 50, 560, 380);
    m_trackersDisplay->buffer(m_trackersBuffer);
    m_trackersDisplay->textfont(FL_COURIER);
    m_trackersDisplay->textsize(12);
    
    m_trackersTab->end();
}

void TorrentDetailsDialog::createPeersTab() {
    m_peersTab = new Fl_Group(10, 35, 580, 405, "Peers");
    m_peersTab->hide();
    m_peersTab->begin();
    
    m_peersBuffer = new Fl_Text_Buffer();
    m_peersDisplay = new Fl_Text_Display(20, 50, 560, 380);
    m_peersDisplay->buffer(m_peersBuffer);
    m_peersDisplay->textfont(FL_COURIER);
    m_peersDisplay->textsize(11);
    
    m_peersTab->end();
}

void TorrentDetailsDialog::createSecurityTab() {
    m_securityTab = new Fl_Group(10, 35, 580, 405, "Security");
    m_securityTab->hide();
    m_securityTab->begin();

    m_securityShield = new SecurityShieldBox(20, 50, 24, 24);
    m_securityShield->setRisk(RiskLevel::Unknown,
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

void TorrentDetailsDialog::createFilesTab() {
    m_filesTab = new Fl_Group(10, 35, 580, 405, "Files");
    m_filesTab->hide();
    m_filesTab->begin();
    
    // The file list is informational here. Security checks run before the
    // download starts, so this tab no longer offers post-download actions.
    m_filesBrowser = new Fl_Browser(20, 50, 560, 380);
    m_filesBrowser->type(FL_HOLD_BROWSER);
    m_filesBrowser->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
    m_filesBrowser->textfont(FL_COURIER);
    m_filesBrowser->textsize(11);
    
    m_filesTab->end();
}

void TorrentDetailsDialog::createButtons() {
    int btn_y = 450;
    int btn_w = 80;
    int btn_h = 30;
    
    m_refreshButton = new Fl_Button(400, btn_y, btn_w, btn_h, "Refresh");
    m_refreshButton->callback(onRefresh, this);
    
    m_closeButton = new Fl_Button(490, btn_y, btn_w, btn_h, "Close");
    m_closeButton->callback(onClose, this);
}

void TorrentDetailsDialog::updateInfo() {
    if (!m_torrent || !m_torrent->isValid()) {
        return;
    }
    
    // Update torrent data
    m_torrent->update();
    
    // Update tabs
    updateGeneralInfo();
    updateTrackers();
    updatePeers();
    updateSecurity();
    updateFiles();
}

void TorrentDetailsDialog::updateGeneralInfo() {
    m_nameLabel->copy_label(formatLabel("Name", m_torrent->getName()).c_str());
    m_sizeLabel->copy_label(formatLabel("Size", m_torrent->formatSize(m_torrent->getTotalSize())).c_str());
    m_statusLabel->copy_label(formatLabel("Status", m_torrent->getStateString()).c_str());
    
    std::ostringstream progress;
    progress << std::fixed << std::setprecision(2) << (m_torrent->getProgress() * 100.0) << "%";
    m_downloadedLabel->copy_label(formatLabel("Downloaded", 
        m_torrent->formatSize(m_torrent->getDownloaded()) + " (" + progress.str() + ")").c_str());
    
    m_uploadedLabel->copy_label(formatLabel("Uploaded", 
        m_torrent->formatSize(m_torrent->getUploaded())).c_str());
    
    std::ostringstream ratio;
    ratio << std::fixed << std::setprecision(3) << m_torrent->getRatio();
    m_ratioLabel->copy_label(formatLabel("Ratio", ratio.str()).c_str());
    
    m_downloadSpeedLabel->copy_label(formatLabel("Download Speed", 
        m_torrent->formatSpeed(m_torrent->getDownloadRate())).c_str());
    
    m_uploadSpeedLabel->copy_label(formatLabel("Upload Speed", 
        m_torrent->formatSpeed(m_torrent->getUploadRate())).c_str());
    
    m_peersLabel->copy_label(formatLabel("Connected Peers", 
        std::to_string(m_torrent->getNumPeers())).c_str());
    
    m_seedsLabel->copy_label(formatLabel("Seeds in Swarm", 
        std::to_string(m_torrent->getNumSeeds())).c_str());
    
    m_etaLabel->copy_label(formatLabel("ETA", m_torrent->getETAString()).c_str());
    
    m_savePathLabel->copy_label(formatLabel("Save Path", m_torrent->getSavePath()).c_str());
    
    m_hashLabel->copy_label(formatLabel("Info Hash", m_torrent->getHash()).c_str());
}

void TorrentDetailsDialog::updateTrackers() {
    auto trackers = m_torrent->getTrackers();
    std::ostringstream oss;
    
    oss << std::left << std::setw(40) << "URL" << " | " 
        << std::setw(20) << "Status" << "\n";
    oss << std::string(70, '-') << "\n";
    
    for (const auto& t : trackers) {
        oss << std::left << std::setw(40) << t.url.substr(0, 39) << " | " 
            << std::setw(20) << t.status << "\n";
    }
    
    m_trackersBuffer->text(oss.str().c_str());
}

void TorrentDetailsDialog::updatePeers() {
    auto peers = m_torrent->getPeers();
    std::ostringstream oss;
    
    oss << std::left << std::setw(18) << "IP" << " | " 
        << std::setw(20) << "Client" << " | "
        << std::setw(10) << "Down" << " | "
        << std::setw(10) << "Up" << " | "
        << "Prg" << "\n";
    oss << std::string(75, '-') << "\n";
    
    for (const auto& p : peers) {
        oss << std::left << std::setw(18) << p.ip << " | " 
            << std::setw(20) << p.client.substr(0, 19) << " | "
            << std::setw(10) << TorrentItem::formatSpeed(p.downloadRate) << " | "
            << std::setw(10) << TorrentItem::formatSpeed(p.uploadRate) << " | "
            << std::fixed << std::setprecision(1) << (p.progress * 100.0) << "%\n";
    }
    
    m_peersBuffer->text(oss.str().c_str());
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
                out << "  [" << SecurityAnalyzer::severityLabel(finding.severity)
                    << "] "
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
            << SecurityBadge::displayText(
                   m_archiveRunning
                       ? "Reading the archive index..."
                       : m_archiveLine)
            << "\n";
        // The entries the index lists, one per line. Names and sizes only:
        // nothing inside the archive is read. The list is long, so it goes at
        // the end of this buffer where the user can scroll it.
        if (!m_archiveRunning && !m_archiveEntries.empty()) {
            out << "\n";
            const std::size_t cap = 200;
            const std::size_t listed = m_archiveEntries.size() > cap
                                           ? cap
                                           : m_archiveEntries.size();
            for (std::size_t i = 0; i < listed; ++i) {
                out << "  "
                    << SecurityBadge::displayText(
                           SecurityBadge::archiveEntryLine(
                               m_archiveEntries[i]))
                    << "\n";
            }
            if (m_archiveEntries.size() > listed) {
                out << "  ... and "
                    << (m_archiveEntries.size() - listed) << " more\n";
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
                hasArchive = ArchiveInspector::isArchivePath(
                    std::string(storage.file_path(lt::file_index_t(i))));
            }
        }
        if (hasArchive && !m_archiveRunning) m_checkArchiveButton->activate();
        else m_checkArchiveButton->deactivate();
    }
}

void TorrentDetailsDialog::onCheckArchiveClicked(Fl_Widget* w, void* data) {
    (void)w;
    TorrentDetailsDialog* dlg = static_cast<TorrentDetailsDialog*>(data);
    if (dlg) dlg->checkArchive();
}

void TorrentDetailsDialog::checkArchive() {
    if (m_archiveRunning || !m_torrent) return;

    // The first archive in the torrent, so the answer names one file.
    const std::vector<TorrentItem::FileInfo> files = m_torrent->getFiles();
    const TorrentItem::FileInfo* archive = nullptr;
    for (const TorrentItem::FileInfo& file : files) {
        if (ArchiveInspector::isArchivePath(file.path)) {
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
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_archiveCancel = cancel;
    updateSecurity();

    struct Delivery {
        TorrentDetailsDialog* dialog = nullptr;
        std::shared_ptr<bool> alive;
        std::string line;
        std::vector<SecurityFile> entries;
    };

    std::shared_ptr<bool> alive = m_alive;
    std::thread([this, alive, cancel, params, index, path]() {
        std::vector<SecurityFile> entries;
        std::string error;
        std::string note;
        const bool ok = ArchivePreflight::inspect(params, index, entries,
                                                  error, cancel.get(), 120,
                                                  &note);

        Delivery* delivery = new Delivery;
        delivery->dialog = this;
        delivery->alive = alive;
        delivery->line =
            ok ? (note.empty() ? archiveSummary(path, entries)
                               : note + " " + archiveSummary(path, entries))
               : "Archive: the index could not be read (" + error + ").";
        if (ok) delivery->entries = std::move(entries);
        Fl::awake([](void* data) {
            std::unique_ptr<Delivery> delivery(
                static_cast<Delivery*>(data));
            if (!delivery->alive || !*delivery->alive) return;
            TorrentDetailsDialog* dialog = delivery->dialog;
            dialog->m_archiveRunning = false;
            dialog->m_archiveLine = delivery->line;
            dialog->m_archiveEntries = std::move(delivery->entries);
            dialog->updateSecurity();
        }, delivery);
    }).detach();
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
        Fl::awake([](void* data) {
            std::unique_ptr<Delivery> delivery(static_cast<Delivery*>(data));
            if (!delivery->alive || !*delivery->alive) return;
            delivery->dialog->onLocalHashDone(delivery->relativePath,
                                              delivery->ok,
                                              delivery->digestOrError);
        }, delivery);
    }).detach();
}

void TorrentDetailsDialog::onLocalHashDone(const std::string& relativePath,
                                           bool ok, const std::string& digestOrError) {
    m_hashingLocally = false;
    if (!ok) {
        m_reputation = ReputationResult{};
        m_reputation.state = ReputationState::Error;
        m_reputation.message =
            "The completed file could not be hashed: " + digestOrError;
        updateSecurity();
        return;
    }

    m_reputationNote =
        "SHA-256 computed locally from the completed file " + relativePath +
        "; nothing was uploaded.";
    m_reputationSha = digestOrError;
    requestReputation(digestOrError);
}

void TorrentDetailsDialog::requestReputation(const std::string& sha256) {
    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    TorrentDetailsDialog* self = this;
    const ReputationService::Start start = service.request(
        sha256, [self, alive](const ReputationResult& result) {
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
            m_reputation.message =
                "Too many reputation checks are running. Try again in a moment.";
            break;
    }
    updateSecurity();
}

void TorrentDetailsDialog::updateFiles() {
    // Rebuilding the browser every two seconds while it is hidden would throw
    // away the user's selection for no visible benefit. Build once, then only
    // while the tab is selected.
    const bool filesTabSelected = m_tabs && m_tabs->value() == m_filesTab;
    if (!filesTabSelected && m_filesLoaded) {
        return;
    }

    const int previousValue = m_filesBrowser->value();
    std::string previousPath;
    if (previousValue > 0 && previousValue <= static_cast<int>(m_files.size())) {
        previousPath = m_files[previousValue - 1].path;
    }

    auto files = m_torrent->getFiles();

    const auto rowText = [](const TorrentItem::FileInfo& f) {
        std::ostringstream row;
        const std::string& display = f.path.empty() ? f.name : f.path;
        row << SecurityBadge::displayText(display);

        SecurityFile file;
        file.path = display;
        file.size = f.size;
        file.hidden = f.hidden;
        file.executable = f.executable;
        file.symlink = f.symlink;
        file.absolutePath = f.absolutePath;
        file.symlinkPath = f.symlinkPath;
        const std::vector<std::string> tags = SecurityAnalyzer::tagsForFile(file);
        if (!tags.empty()) {
            row << "  [";
            for (std::size_t t = 0; t < tags.size() && t < 3; ++t) {
                if (t) row << ", ";
                row << tags[t];
            }
            row << "]";
        }

        row << "  |  " << TorrentItem::formatSize(f.size)
            << "  |  " << std::fixed << std::setprecision(1)
            << (f.progress * 100.0) << "%";
        return row.str();
    };

    // The file list is stable while a torrent downloads; only the progress
    // text moves. Updating rows in place keeps the selection and the scroll
    // position instead of jumping back to the top every two seconds.
    bool sameRows = files.size() == m_files.size();
    if (sameRows) {
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].path != m_files[i].path || files[i].size != m_files[i].size) {
                sameRows = false;
                break;
            }
        }
    }

    if (sameRows) {
        m_files = files;
        for (size_t i = 0; i < m_files.size(); ++i) {
            m_filesBrowser->replace(static_cast<int>(i) + 1, rowText(m_files[i]).c_str());
        }
    } else {
        m_files = files;
        m_filesBrowser->clear();
        for (const auto& f : m_files) {
            m_filesBrowser->add(rowText(f).c_str());
        }

        int newValue = 0;
        if (!previousPath.empty()) {
            for (size_t i = 0; i < m_files.size(); ++i) {
                if (m_files[i].path == previousPath) {
                    newValue = static_cast<int>(i) + 1;
                    break;
                }
            }
        }
        if (newValue == 0 && previousValue > 0 && !m_files.empty()) {
            newValue = std::min(previousValue, static_cast<int>(m_files.size()));
        }
        if (newValue == 0 && !m_filesLoaded && !m_files.empty()) {
            newValue = 1;       // first build: put the keyboard somewhere useful
        }
        if (newValue > 0) {
            m_filesBrowser->value(newValue);
        }
    }
    m_filesLoaded = true;
}

std::string TorrentDetailsDialog::formatLabel(const std::string& label, const std::string& value) {
    return label + ": " + value;
}

void TorrentDetailsDialog::showSecurityAndCheck() {
    if (m_tabs && m_securityTab) m_tabs->value(m_securityTab);
    checkReputation();
}

void TorrentDetailsDialog::show_modal() {
    show();
    
    while (shown()) {
        Fl::wait();
    }
}

// Static callbacks
void TorrentDetailsDialog::onRefresh(Fl_Widget* w, void* data) {
    (void)w;
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    dlg->updateInfo();
}

void TorrentDetailsDialog::onClose(Fl_Widget* w, void* data) {
    (void)w;
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    dlg->hide();
}

void TorrentDetailsDialog::updateTimerCallback(void* data) {
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    
    if (dlg && dlg->shown()) {
        dlg->updateInfo();
        Fl::repeat_timeout(2.0, updateTimerCallback, data);
    }
}
