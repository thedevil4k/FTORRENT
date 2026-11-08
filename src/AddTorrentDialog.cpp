#include "AddTorrentDialog.h"
#include "AddTorrentDialogArchive.h"
#include "AddTorrentDialogHash.h"
#include "SettingsManager.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"
#include "SecurityReputation.h"
#include "ReputationService.h"
#include "ArchiveCheckTask.h"
#include "ArchiveInspector.h"
#include "AddTorrentDialogReputation.h"
#include "TorrentFileHashTask.h"
#include "Format.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include "PathUtils.h"
#include <libtorrent/version.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <FL/fl_draw.H>
#include <cstdlib>

namespace {

// The green dot that sits at the right end of the magnet field. Drawn rather
// than imaged, so it needs no asset and no theme lookup.
class OriginDot : public Fl_Box {
public:
    OriginDot(int X, int Y, int W, int H)
        : Fl_Box(X, Y, W, H) {
        box(FL_NO_BOX);
    }
    void draw() override {
        fl_color(fl_rgb_color(64, 184, 80));
        fl_pie(x() + 1, y() + 1, w() - 2, h() - 2, 0.0, 6.28318530718);
    }
};

} // namespace

AddTorrentDialog::AddTorrentDialog()
    : Fl_Window(500, 760, "Add Torrent")
    , m_okClicked(false) {
    m_startImmediatelyResult = true;
    m_priority = Priority::Normal;

    createUI();
    createButtons();

    end();
    set_modal();

    loadDefaults();
    m_reputationHelper = std::make_unique<AddTorrentDialogReputation>(this);
    m_fileHashHelper = std::make_unique<AddTorrentDialogFileHash>(this);
    m_archiveHelper = std::make_unique<AddTorrentDialogArchive>(this);
}

AddTorrentDialog::~AddTorrentDialog() {
    // A lookup started from this dialog may still be in flight. Saying so here
    // keeps its completion from touching widgets that no longer exist.
    if (m_alive) *m_alive = false;
    // The archive check stops too, instead of running out its deadline in a
    // swarm nobody is waiting for.
    if (m_archiveTask) m_archiveTask->cancel();
    if (m_fileHashTask) m_fileHashTask->cancel();
}

void AddTorrentDialog::createUI() {
    int y = 20;

    // --- Torrent File Section ---
    Fl_Box* fileLabel = new Fl_Box(20, y, 100, 25, "Torrent File:");
    fileLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;

    m_filePathInput = new Fl_Input(20, y, 370, 30);
    m_browseFile = new Fl_Button(400, y, 80, 30, "Browse...");
    m_browseFile->callback(onBrowseFile, this);
    y += 45;

    // --- Magnet Link Section ---
    Fl_Box* magnetLabel = new Fl_Box(20, y, 100, 25, "Magnet Link:");
    magnetLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;

    m_magnetInput = new Fl_Input(20, y, 460, 30);
    m_magnetInput->tooltip("Paste magnet link here (magnet:?xt=...)");
    // Marks the magnet field while this import is the one going through it.
    m_magnetOriginDot = new OriginDot(462, y + 9, 12, 12);
    m_magnetOriginDot->hide();
    // FL_WHEN_CHANGED so the dot follows what is actually in the field while
    // the user types or pastes, instead of only on release.
    m_magnetInput->when(FL_WHEN_CHANGED);
    m_magnetInput->callback(onMagnetFieldChanged, this);
    y += 55;

    // --- Save Path Section ---
    Fl_Box* saveLabel = new Fl_Box(20, y, 100, 25, "Save to:");
    saveLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;

    m_savePathInput = new Fl_Input(20, y, 370, 30);
    m_browseSavePath = new Fl_Button(400, y, 80, 30, "Browse...");
    m_browseSavePath->callback(onBrowseSavePath, this);
    y += 50;

    // --- Options Section ---
    m_startImmediately =
        new Fl_Check_Button(20, y, 200, 25, "Start download immediately");
    m_startImmediately->value(1);

    Fl_Box* priorityLabel = new Fl_Box(250, y, 80, 25, "Priority:");
    priorityLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_priorityChoice = new Fl_Choice(330, y, 100, 25);
    m_priorityChoice->add("Normal");
    m_priorityChoice->add("High");
    m_priorityChoice->add("Low");
    m_priorityChoice->value(0); // Normal
    y += 40;

    // --- Files Preview Section ---
    Fl_Box* filesLabel =
        new Fl_Box(20, y, 400, 25, "Files to download (uncheck to skip):");
    filesLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;

    m_fileBrowser = new Fl_Check_Browser(20, y, 460, 150);
    m_fileBrowser->textsize(12);
    m_fileBrowser->when(FL_WHEN_CHANGED);
    m_fileBrowser->callback(onFileSelectionChanged, this);
    y += 160;

    // --- Security panel ---
    m_securityPanel = new Fl_Box(20, y, 460, 150);
    m_securityPanel->box(FL_THIN_DOWN_BOX);
    m_securityPanel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_securityShield = new SecurityShieldBox(30, y + 10, 20, 20);
    m_securityShield->setRisk(RiskLevel::Unknown,
                              "No file list is available yet.");
    // The shield is the only thing that starts a reputation lookup.
    m_securityShield->onClick([this]() { checkReputation(); });

    m_securitySummary = new Fl_Box(56, y + 7, 412, 20);
    m_securitySummary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_securitySummary->labelfont(FL_HELVETICA_BOLD);

    m_securityDetail = new Fl_Box(56, y + 27, 412, 32);
    m_securityDetail->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_securityDetail->labelsize(11);

    m_securityHint = new Fl_Box(30, y + 60, 440, 16,
                                "Metadata only. No file contents were read.");
    m_securityHint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_securityHint->labelsize(10);
    m_securityHint->labelcolor(fl_rgb_color(140, 140, 140));

    m_selectNonExecutables =
        new Fl_Button(30, y + 78, 220, 26, "Select only non-executables");
    m_selectNonExecutables->callback(onSelectNonExecutables, this);
    m_selectNonExecutables->tooltip("Uncheck files that run code: executables, "
                                    "scripts, shortcuts and macros");

    // Reputation lives below the metadata analysis: it is a second, explicit
    // step, and it says why it cannot run when there is no whole-file hash.
    m_reputationLabel = new Fl_Box(30, y + 108, 440, 34);
    m_reputationLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_reputationLabel->labelsize(11);
    m_reputationLabel->copy_label(reputationLine(ReputationResult{}).c_str());

    // Second explicit check: read a RAR/ZIP's index without downloading the
    // archive. Disabled until there is an archive in the selection.
    m_checkArchive = new Fl_Button(260, y + 78, 200, 26, "Check archive");
    m_checkArchive->callback(onCheckArchive, this);
    m_checkArchive->tooltip(
        "Reads only the archive index (first and last piece for ZIP/RAR5, a "
        "bounded header walk for older RAR) to list what "
        "is inside. The highlighted .zip/.rar is used; it does not need to be "
        "checked for download. Saved to a temporary folder and deleted "
        "afterwards.");
    m_checkArchive->deactivate();

    // Third explicit check: hash one selected file's pieces. Only one file at
    // a time, only when the user clicks. The file's pieces are read into a
    // throwaway dir (quarantine when the user asks), hashed, then deleted.
    // Nothing is executed and nothing is uploaded.
    m_calculateHashButton =
        new Fl_Button(20, y + 78, 230, 26, "Calculate hash");
    m_calculateHashButton->callback(onCalculateHash, this);
    m_calculateHashButton->tooltip(
        "Hashes only the pieces that cover the highlighted file. Not the whole "
        "torrent: just that file. If the metadata already carries the whole-"
        "file SHA-256 it is shown without downloading anything. Otherwise the "
        "file's pieces are fetched to a throwaway folder and deleted after "
        "hashing. Optional quarantine isolates the temp folder. Experimental.");
    m_calculateHashButton->deactivate();

    // What the last check listed, under the panel. Hidden until there is
    // something to show: the dialog keeps its size while nothing is known.
    m_archiveBrowser = new Fl_Hold_Browser(30, y + 152, 440, 76);
    m_archiveBrowser->textsize(11);
    m_archiveBrowser->hide();
}

void AddTorrentDialog::createButtons() {
    int btn_y = 712;
    int btn_w = 80;
    int btn_h = 30;

    m_okButton = new Fl_Button(300, btn_y, btn_w, btn_h, "OK");
    m_okButton->callback(onOK, this);

    m_cancelButton = new Fl_Button(390, btn_y, btn_w, btn_h, "Cancel");
    m_cancelButton->callback(onCancel, this);
}

void AddTorrentDialog::loadDefaults() {
    auto& settings = SettingsManager::instance();
    m_savePathInput->value(settings.getDefaultSavePath().c_str());
}

bool AddTorrentDialog::validate() {
    m_torrentPath = m_filePathInput->value();
    m_magnetLink = m_magnetInput->value();

    // Validate torrent source (at least one must be provided)
    if (m_torrentPath.empty() && m_magnetLink.empty()) {
        fl_alert("Please select a torrent file or enter a magnet link");
        return false;
    }

    if (!m_magnetLink.empty()) {
        if (m_magnetLink.substr(0, 8) != "magnet:?") {
            fl_alert("Invalid magnet link format");
            return false;
        }
        // If magnet is provided, we prioritize it or ensure m_torrentPath is
        // clear if we only want one
        if (!m_torrentPath.empty()) {
            fl_message("Both file and magnet provided. Prioritizing the "
                       "torrent file.");
            m_magnetLink = "";
        }
    }

    // Validate save path
    if (strlen(m_savePathInput->value()) == 0) {
        fl_alert("Please select a download directory");
        return false;
    }
    m_savePath = m_savePathInput->value();

    // Validate file selection. Priorities are indexed by the libtorrent file
    // index, not by the visible row: pad files occupy indices but have no row,
    // and getting this wrong shifts every later file by one.
    m_filePriorities.clear();
    bool anySelected = false;
    if (!m_files.empty() && m_totalFileCount > 0) {
        m_filePriorities.assign(static_cast<std::size_t>(m_totalFileCount), 0);
        for (int row = 0; row < static_cast<int>(m_files.size()); ++row) {
            if (!m_fileBrowser->checked(row + 1)) continue;
            anySelected = true;
            int index = m_files[static_cast<std::size_t>(row)].fileIndex;
            if (index < 0) index = row;
            if (index >= 0 && index < m_totalFileCount) {
                m_filePriorities[static_cast<std::size_t>(index)] = 1;
            }
        }
    }

    if (!m_files.empty() && !anySelected) {
        fl_alert("Please select at least one file to download");
        return false;
    }

    updateSecurityPanel();
    if (!confirmRiskySelection()) {
        return false;
    }

    switch (m_priorityChoice->value()) {
        case 0:
            m_priority = Priority::Normal;
            break;
        case 1:
            m_priority = Priority::High;
            break;
        case 2:
            m_priority = Priority::Low;
            break;
        default:
            m_priority = Priority::Normal;
    }

    return true;
}

bool AddTorrentDialog::confirmRiskySelection() {
    if (!SecurityBadge::hasWarning(m_selectedReport.level)) return true;

    std::string message =
        "The selected files have high-risk metadata indicators.\n\n";
    message += m_selectedReport.summary;
    message += "\n\nDownload them anyway?";

    // The first button is the non-destructive one. fl_choice() returns 0 for
    // it and 1 for the explicit "Download anyway".
    return fl_choice("%s", "Cancel", "Download anyway", nullptr,
                     message.c_str()) == 1;
}

bool AddTorrentDialog::show_modal() {
    m_okClicked = false;
    updateOriginDot();
    show();

    while (shown()) {
        Fl::wait();
    }

    return m_okClicked;
}

// Static callbacks
void AddTorrentDialog::onBrowseFile(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;

    const char* filename = PathUtils::showFileChooser(
        "Select Torrent File", "Torrent Files (*.torrent)", nullptr,
        Fl_File_Chooser::SINGLE);

    if (filename) {
        dlg->m_filePathInput->value(filename);
        dlg->updateFileList(filename);
        dlg->updateOriginDot();
    }
}

void AddTorrentDialog::onBrowseSavePath(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;

    const char* dir = PathUtils::showDirChooser("Select Download Directory",
                                                dlg->m_savePathInput->value());

    if (dir) {
        dlg->m_savePathInput->value(dir);
    }
}

void AddTorrentDialog::onOK(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;

    if (dlg->validate()) {
        dlg->m_okClicked = true;
        dlg->hide();
    }
}

void AddTorrentDialog::onCancel(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;
    dlg->m_okClicked = false;
    dlg->hide();
}

void AddTorrentDialog::setTorrentPath(const std::string& path) {
    if (m_filePathInput) {
        m_filePathInput->value(path.c_str());
        updateFileList(path);
        updateOriginDot();
    }
}

void AddTorrentDialog::setMagnetLink(const std::string& magnet) {
    if (m_magnetInput) {
        m_magnetInput->value(magnet.c_str());
        updateOriginDot();
    }
}

// Shows the dot only while the magnet is what validate() would actually use:
// a .torrent file wins over a magnet when both are filled in, so a dot next
// to the magnet field while a file is chosen would be a lie.
void AddTorrentDialog::updateOriginDot() {
    if (!m_magnetOriginDot) return;
    const char* magnet = m_magnetInput ? m_magnetInput->value() : nullptr;
    const char* file = m_filePathInput ? m_filePathInput->value() : nullptr;
    bool byMagnet = magnet && *magnet && (!file || !*file);
    if (byMagnet == (m_magnetOriginDot->visible() != 0)) return;
    if (byMagnet) m_magnetOriginDot->show();
    else m_magnetOriginDot->hide();
    m_magnetOriginDot->redraw();
}

void AddTorrentDialog::onMagnetFieldChanged(Fl_Widget*, void* data) {
    ((AddTorrentDialog*)data)->updateOriginDot();
}

void AddTorrentDialog::setSavePath(const std::string& path) {
    if (m_savePathInput) {
        m_savePathInput->value(path.c_str());
    }
}

void AddTorrentDialog::updateFileList(const std::string& torrentPath) {
    if (!m_fileBrowser) return;
    m_fileBrowser->clear();
    m_files.clear();
    m_totalFileCount = 0;

    if (torrentPath.empty()) {
        updateSecurityPanel();
        return;
    }

    try {
        // load_torrent_file() works on libtorrent 2.0 and 2.1
        // (the torrent_info filename constructor was removed in 2.1).
        lt::add_torrent_params atp = lt::load_torrent_file(torrentPath);
#if LIBTORRENT_VERSION_NUM >= 20100
        // torrent_info::files() was removed in 2.1, use layout() instead.
        auto const& fs = atp.ti->layout();
#else
        auto const& fs = atp.ti->files();
#endif

        std::vector<TorrentFileEntry> files;
        for (int i = 0; i < fs.num_files(); ++i) {
            const lt::file_index_t index(i);
            // Pad files are piece alignment, not content. They still count
            // towards the priority vector, which is why totalFileCount is
            // taken from num_files() and not from the visible rows.
            if (fs.pad_file_at(index)) continue;

            TorrentFileEntry file;
            file.fileIndex = i;
            // file_path(), not file_name(): file_name() is only the leaf and
            // would hide both the folder and duplicate names.
            file.path = std::string(fs.file_path(index));
            file.size = fs.file_size(index);
            const auto flags = fs.file_flags(index);
            file.hidden =
                static_cast<bool>(flags & lt::file_storage::flag_hidden);
            file.executable =
                static_cast<bool>(flags & lt::file_storage::flag_executable);
            file.symlink =
                static_cast<bool>(flags & lt::file_storage::flag_symlink);
            file.absolutePath = fs.file_absolute_path(index);
            if (file.symlink) file.symlinkPath = fs.symlink(index);
            files.push_back(std::move(file));
        }
        showFiles(files, fs.num_files());
    } catch (const std::exception& e) {
        m_files.clear();
        m_totalFileCount = 0;
        m_fileBrowser->add(("Error: " + std::string(e.what())).c_str(), 0);
        updateSecurityPanel();
    }
}

void AddTorrentDialog::setFileList(const std::vector<TorrentFileEntry>& files) {
    if (!m_fileBrowser) return;

    // The search row already resolved the metadata. Its fileIndex values are
    // libtorrent indices, so the priority vector must cover every index,
    // including the pad files that have no row here.
    int total = static_cast<int>(files.size());
    for (const TorrentFileEntry& file : files) {
        total = std::max(total, file.fileIndex + 1);
    }
    showFiles(files, total);
}

void AddTorrentDialog::showFiles(const std::vector<TorrentFileEntry>& files,
                                 int totalFileCount) {
    if (!m_fileBrowser) return;

    m_fileBrowser->clear();
    m_files.clear();
    // A new list means new hashes; any answer from the old one is stale.
    m_reputation = ReputationResult{};
    m_reputationSha.clear();
    m_checkLine.clear();
    m_files.reserve(files.size());
    for (const TorrentFileEntry& file : files) {
        if (file.padFile) continue;
        m_files.push_back(file);
        m_fileBrowser->add(fileRowLabel(file).c_str(), 1); // checked
    }
    m_totalFileCount =
        std::max(totalFileCount, static_cast<int>(m_files.size()));
    updateSecurityPanel();
    m_fileBrowser->redraw();
}

std::string AddTorrentDialog::fileRowLabel(const TorrentFileEntry& file) const {
    std::string label = SecurityBadge::displayText(file.path);
    label += " (";
    label += Format::bytes(file.size);
    label += ")";

    const std::vector<std::string> tags = SecurityAnalyzer::tagsForFile(file);
    if (!tags.empty()) {
        label += "  [";
        for (std::size_t i = 0; i < tags.size() && i < 3; ++i) {
            if (i) label += ", ";
            label += tags[i];
        }
        label += "]";
    }
    return label;
}

std::vector<TorrentFileEntry> AddTorrentDialog::selectedFiles() const {
    std::vector<TorrentFileEntry> selected;
    if (!m_fileBrowser) return selected;

    for (int row = 0; row < static_cast<int>(m_files.size()); ++row) {
        if (m_fileBrowser->checked(row + 1)) {
            selected.push_back(m_files[static_cast<std::size_t>(row)]);
        }
    }
    return selected;
}

void AddTorrentDialog::updateSecurityPanel() {
    if (!m_securityShield) return;

    const std::vector<TorrentFileEntry> selected = selectedFiles();
    m_selectedReport = SecurityAnalyzer::analyze(selected);

    // A reputation answer belongs to one hash. When the selection changes the
    // hash, the old answer is forgotten rather than shown against another file.
    {
        const ReputationTarget target = findReputationTarget(selected);
        if (target.sha256 != m_reputationSha) {
            m_reputationSha = target.sha256;
            m_reputation = ReputationResult{};
        }
        // The small print is not tied to a provider: it says which hash the
        // metadata carries as soon as the selection is known.
        m_torrentSha256 = target.sha256;
        m_torrentSha256Path = target.path;
    }

    // An empty report means either "no metadata at all" or "the user
    // unchecked everything". They are different situations and should not
    // share the "expand the search row" hint.
    const bool hasList = !m_files.empty();
    const bool hasSelection = !selected.empty();

    RiskLevel level = m_selectedReport.level;
    std::string summary = m_selectedReport.summary;
    if (hasList && !hasSelection) {
        level = RiskLevel::Unknown;
        summary = "No files selected. Metadata risk is unknown.";
    }

    m_securityShield->setRisk(level,
                              SecurityBadge::riskLabel(level) + ". " + summary);

    if (m_securitySummary) {
        m_securitySummary->copy_label(
            (SecurityBadge::riskLabel(level) + "  -  " + summary).c_str());
        m_securitySummary->labelcolor(SecurityBadge::riskColor(level));
    }

    if (m_securityDetail) {
        std::string detail;
        int shown = 0;
        for (const Finding& finding : m_selectedReport.findings) {
            if (shown >= 3) break;
            if (!detail.empty()) detail += "\n";
            detail += SecurityBadge::displayText(
                SecurityAnalyzer::describeFinding(finding));
            ++shown;
        }
        if (detail.empty()) {
            if (!hasList) {
                detail = "No file list available. Metadata risk is unknown.";
            } else if (!hasSelection) {
                detail = "Select at least one file to analyze its metadata.";
            } else {
                detail = "No known indicators in file names or metadata.";
            }
        }
        m_securityDetail->copy_label(detail.c_str());
        m_securityDetail->labelcolor(SecurityBadge::riskColor(level));
    }

    if (m_securityHint) {
        if (!hasList) {
            m_securityHint->copy_label(
                "No metadata yet. Expand the search row first.");
        } else if (!hasSelection) {
            m_securityHint->copy_label(
                "Nothing is selected, so there is nothing to download.");
        } else {
            m_securityHint->copy_label(
                "Metadata only. No file contents were read.");
        }
    }

    if (m_selectNonExecutables) {
        if (hasList) m_selectNonExecutables->activate();
        else m_selectNonExecutables->deactivate();
    }

    if (m_checkArchive) {
        const bool hasArchive = m_archiveHelper->target() != nullptr;
        if (hasArchive && !m_archiveRunning) m_checkArchive->activate();
        else m_checkArchive->deactivate();
    }

    // Hash-by-file targets any selectable file, archive or not. Disabled while
    // a hash is already running; enabled when there is at least one file row.
    if (m_calculateHashButton) {
        const bool hasFile = !m_files.empty();
        if (hasFile && !m_fileHashRunning && !m_archiveRunning)
            m_calculateHashButton->activate();
        else
            m_calculateHashButton->deactivate();
    }

    m_reputationHelper->refreshReputationLabel();
}

void AddTorrentDialog::onCheckArchive(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = static_cast<AddTorrentDialog*>(data);
    if (dlg) dlg->checkArchive();
}

void AddTorrentDialog::checkArchive() {
    if (m_archiveHelper) m_archiveHelper->checkArchive();
}

void AddTorrentDialog::calculateHash() {
    if (m_fileHashHelper) m_fileHashHelper->calculateHash();
}

void AddTorrentDialog::onCalculateHash(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = static_cast<AddTorrentDialog*>(data);
    if (dlg) dlg->calculateHash();
}

void AddTorrentDialog::onFileSelectionChanged(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = static_cast<AddTorrentDialog*>(data);
    if (dlg) dlg->updateSecurityPanel();
}

void AddTorrentDialog::onSelectNonExecutables(Fl_Widget*, void* data) {
    AddTorrentDialog* dlg = static_cast<AddTorrentDialog*>(data);
    if (!dlg || !dlg->m_fileBrowser) return;

    // Archives and disk images stay checked: they are containers, not code,
    // and dropping them would silently lose the content the user asked for.
    for (int row = 0; row < static_cast<int>(dlg->m_files.size()); ++row) {
        const bool runnable = SecurityAnalyzer::isRunnable(
            dlg->m_files[static_cast<std::size_t>(row)]);
        dlg->m_fileBrowser->checked(row + 1, runnable ? 0 : 1);
    }
    dlg->m_fileBrowser->redraw();
    dlg->updateSecurityPanel();
}

void AddTorrentDialog::checkReputation() {
    const std::vector<TorrentFileEntry> selected = selectedFiles();
    const ReputationTarget target =
        m_reputationHelper->prepareReputation(selected);

    if (!target.found()) {
        m_reputationSha.clear();
        m_reputation = ReputationService::noHashResult();
        m_reputationHelper->refreshReputationLabel();
        return;
    }

    m_reputationSha = target.sha256;
    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    AddTorrentDialog* self = this;
    const ReputationService::Start start = service.request(
        target.sha256, [self, alive](const ReputationResult& result) {
            if (!alive || !*alive) return;
            self->m_reputation = result;
            self->m_reputationHelper->refreshReputationLabel();
        });

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
                "Too many reputation checks are running. "
                "Try again in a moment.";
            break;
    }
    m_reputationHelper->refreshReputationLabel();
}

std::optional<lt::add_torrent_params>
AddTorrentDialog::buildAddTorrentParams() {
    const std::string torrentPath = m_filePathInput &&
                                            m_filePathInput->value()
                                        ? m_filePathInput->value()
                                        : "";
    const std::string magnet =
        m_magnetInput && m_magnetInput->value() ? m_magnetInput->value()
                                               : "";
    std::string body = m_torrentBody;
    // A body that describes a different torrent than the magnet on screen must
    // never be used: the answer would be about someone else's files.
    if (!body.empty() && !magnet.empty()) {
        const std::string expected = TorrentFileList::magnetHash(magnet);
        const std::string actual = TorrentFileList::infoHashOfTorrentFile(body);
        if (!expected.empty() && !actual.empty() && expected != actual) {
            body.clear();
        }
    }
    const std::string torrentUrl = m_torrentUrl;
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
