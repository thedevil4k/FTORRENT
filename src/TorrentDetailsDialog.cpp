#include "TorrentDetailsDialog.h"
#include "ReputationService.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"
#include "SecurityReputation.h"
#include <FL/Fl.H>
#include <algorithm>
#include <iomanip>
#include <sstream>

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

    m_securitySummary = new Fl_Box(54, 50, 526, 24);
    m_securitySummary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_securitySummary->labelfont(FL_HELVETICA_BOLD);

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
        << SecurityBadge::displayText(reputationLine(m_reputation)) << "\n";

    m_securityBuffer->text(out.str().c_str());
}

void TorrentDetailsDialog::checkReputation() {
    const std::vector<SecurityFile> files = m_torrent->getSecurityFiles();
    const ReputationTarget target = findReputationTarget(files);

    if (!target.found()) {
        // No whole-file hash in the metadata: the exact required sentence,
        // instead of a check that would have to download the file first.
        m_reputationSha.clear();
        m_reputation = ReputationService::noHashResult();
        updateSecurity();
        return;
    }

    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    TorrentDetailsDialog* self = this;
    const ReputationService::Start start = service.request(
        target.sha256, [self, alive](const ReputationResult& result) {
            if (!alive || !*alive) return;
            self->m_reputation = result;
            self->updateSecurity();
        });

    m_reputationSha = target.sha256;
    switch (start) {
        case ReputationService::Start::Started:
        case ReputationService::Start::AlreadyRunning:
            m_reputation = ReputationResult{};
            m_reputation.state = ReputationState::Querying;
            m_reputation.sha256 = target.sha256;
            break;
        case ReputationService::Start::Cached:
            m_reputation = service.find(target.sha256);
            break;
        case ReputationService::Start::Rejected:
            m_reputation = ReputationResult{};
            m_reputation.state = ReputationState::Error;
            m_reputation.sha256 = target.sha256;
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
