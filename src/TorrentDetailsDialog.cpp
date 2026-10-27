#include "TorrentDetailsDialog.h"
#include "ArchiveContentsDialog.h"
#include "ArchiveInspector.h"
#include "FileHash.h"
#include "SystemUtils.h"
#include "VirusTotal.h"
#include <FL/Fl.H>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {

std::filesystem::path pathFromUtf8(const std::string& text) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::u8path(text);
#endif
}

std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
    return path.u8string();
#endif
}

// Torrent entry names are attacker-controlled. Replacing control characters
// keeps a crafted name from breaking one browser row into several.
std::string displayText(std::string text) {
    for (char& ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte == 0x7f) ch = '?';
    }
    return text;
}

// A worker's result. The dialog can be closed while a large file is still
// being hashed, so the delivery carries the cancellation flag shared with the
// dialog rather than trusting a raw pointer. deliverHash() only dereferences
// the target when that flag is still false, on the UI thread.
struct HashDelivery {
    std::shared_ptr<std::atomic<bool>> cancel;
    TorrentDetailsDialog* target = nullptr;
    bool ok = false;
    std::string path;
    std::string hash;
    std::string error;
};

} // namespace

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

    // A hashing worker may still be reading a large file. Ask it to stop and
    // make sure any result it later posts is dropped by deliverHash().
    if (m_hashCancel) {
        m_hashCancel->store(true);
    }
    
    delete m_trackersBuffer;
    delete m_peersBuffer;
}

void TorrentDetailsDialog::createGeneralTab() {
    m_generalTab = new Fl_Group(10, 35, 580, 405, "General");
    m_generalTab->begin();
    
    int y = 50;
    int label_h = 20;
    int spacing = 25;
    
    m_nameLabel = new Fl_Box(20, y, 560, label_h);
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

void TorrentDetailsDialog::createFilesTab() {
    m_filesTab = new Fl_Group(10, 35, 580, 405, "Files");
    m_filesTab->hide();
    m_filesTab->begin();
    
    // A list, not a text blob: a file has to be selectable before its hash can
    // be computed. The extra text (size and progress) is still in each row.
    m_filesBrowser = new Fl_Browser(20, 50, 560, 320);
    m_filesBrowser->type(FL_HOLD_BROWSER);
    m_filesBrowser->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
    m_filesBrowser->textfont(FL_COURIER);
    m_filesBrowser->textsize(11);
    m_filesBrowser->callback(onFileSelected, this);

    m_virusTotalButton = new Fl_Button(20, 378, 170, 30, "Open in VirusTotal");
    m_virusTotalButton->callback(onVirusTotal, this);
    m_virusTotalButton->tooltip(
        "Compute the selected file's SHA-256 locally and open its VirusTotal report. "
        "The file is not uploaded; the hash is sent to VirusTotal when the report opens.");
    m_virusTotalButton->deactivate();

    m_virusTotalStatus = new Fl_Box(198, 378, 382, 54);
    m_virusTotalStatus->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_virusTotalStatus->labelsize(11);
    m_virusTotalStatus->copy_label(
        "Select a completed .exe file, or a RAR/ZIP archive to inspect.");
    
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
        row << displayText(display);
        if (isExecutablePath(display)) {
            row << "  [EXE]";
        } else if (ArchiveInspector::isArchivePath(display)) {
            row << "  [ARCHIVE]";
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

    m_rebuildingFiles = true;
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
    m_rebuildingFiles = false;
    m_filesLoaded = true;
    updateVirusTotalControls();
}

std::string TorrentDetailsDialog::extensionLower(const std::string& path) {
    std::string extension = pathToUtf8(pathFromUtf8(path).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool TorrentDetailsDialog::isExecutablePath(const std::string& path) {
    return extensionLower(path) == ".exe";
}

std::string TorrentDetailsDialog::resolveLocalPath(const std::string& relativePath) const {
    const std::filesystem::path relative = pathFromUtf8(relativePath);
    if (relative.is_absolute()) {
        return pathToUtf8(relative);
    }

    const std::filesystem::path base = pathFromUtf8(m_torrent->getSavePath());
    std::error_code ec;
    const std::filesystem::path candidate = base / relative;
    if (std::filesystem::is_regular_file(candidate, ec)) {
        return pathToUtf8(candidate);
    }

    // libtorrent normally includes the torrent's root directory in
    // file_path(). Older layouts did not, so try the documented on-disk shape
    // as a fallback rather than refusing to hash a file that is right there.
    const std::filesystem::path withName =
        base / pathFromUtf8(m_torrent->getName()) / relative;
    if (std::filesystem::is_regular_file(withName, ec)) {
        return pathToUtf8(withName);
    }
    return pathToUtf8(candidate);
}

void TorrentDetailsDialog::updateVirusTotalControls() {
    if (!m_virusTotalButton || !m_virusTotalStatus) {
        return;
    }

    const auto setStatus = [this](const char* text) {
        if (!m_statusSticky) {
            m_virusTotalStatus->copy_label(text);
        }
    };

    if (m_hashRunning) {
        m_virusTotalButton->deactivate();
        return;
    }

    const int value = m_filesBrowser->value();
    if (value <= 0 || value > static_cast<int>(m_files.size())) {
        m_virusTotalButton->label("Open in VirusTotal");
        m_virusTotalButton->deactivate();
        setStatus(
            "Select a completed .exe file, or a RAR/ZIP archive to inspect.");
        return;
    }

    const TorrentItem::FileInfo& file = m_files[value - 1];
    const std::string extension = extensionLower(file.path);
    const bool archive = ArchiveInspector::isArchivePath(file.path);

    m_virusTotalButton->label(archive ? "Inspect Archive" : "Open in VirusTotal");
    if (archive) {
        m_virusTotalButton->tooltip(
            "List the .exe files inside this archive and open the VirusTotal report "
            "for one. Nothing is extracted or executed.");
    } else {
        m_virusTotalButton->tooltip(
            "Compute the selected file's SHA-256 locally and open its VirusTotal report. "
            "The file is not uploaded; the hash is sent to VirusTotal when the report opens.");
    }

    if (!archive && extension != ".exe") {
        m_virusTotalButton->deactivate();
        if (extension == ".7z") {
            setStatus("Only .exe files and RAR/ZIP archives can be checked. .7z is not supported.");
        } else {
            setStatus("Only .exe files and RAR/ZIP archives can be checked.");
        }
        return;
    }

    if (!file.complete) {
        m_virusTotalButton->deactivate();
        if (archive) {
            setStatus(
                "Wait until this archive finishes downloading. A partial archive cannot be read safely.");
        } else {
            setStatus(
                "Wait until this file finishes downloading. A partial file has no useful SHA-256.");
        }
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(
            pathFromUtf8(resolveLocalPath(file.path)), ec)) {
        m_virusTotalButton->deactivate();
        setStatus("The file is marked complete but cannot be read from its save path.");
        return;
    }

    if (archive) {
        if (!ArchiveInspector::isAvailable()) {
            m_virusTotalButton->deactivate();
            setStatus(
                "This build has no RAR/ZIP reader. Rebuild with libarchive to inspect this archive.");
            return;
        }

        m_virusTotalButton->activate();
        setStatus(
            "Open the archive to choose an .exe inside it. The archive is read-only; nothing is extracted or executed.");
        return;
    }

    m_virusTotalButton->activate();
    setStatus(
        "SHA-256 is computed locally. Opening the report sends the hash to VirusTotal; the file is not uploaded.");
}

void TorrentDetailsDialog::openArchiveContents(int fileIndex) {
    if (m_hashRunning) {
        return;
    }
    if (fileIndex <= 0 || fileIndex > static_cast<int>(m_files.size())) {
        m_virusTotalStatus->copy_label("Select an archive first.");
        return;
    }

    const TorrentItem::FileInfo file = m_files[fileIndex - 1];
    if (!ArchiveInspector::isArchivePath(file.path)) {
        m_virusTotalStatus->copy_label("Select a RAR or ZIP archive to inspect.");
        return;
    }
    if (!file.complete) {
        m_virusTotalStatus->copy_label(
            "Wait until the archive finishes downloading before inspecting it.");
        return;
    }
    if (!ArchiveInspector::isAvailable()) {
        m_virusTotalStatus->copy_label(
            "This build has no RAR/ZIP reader. Rebuild with libarchive to inspect this archive.");
        return;
    }

    const std::string localPath = resolveLocalPath(file.path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(pathFromUtf8(localPath), ec)) {
        m_virusTotalStatus->copy_label(
            "The downloaded archive is missing or is not a regular file. Check the save path.");
        return;
    }

    const std::string displayPath = file.path.empty() ? file.name : file.path;
    auto* dialog = new ArchiveContentsDialog(localPath, displayPath);
    dialog->show_modal();
    delete dialog;
}

void TorrentDetailsDialog::startVirusTotalCheck(int fileIndex) {
    if (m_hashRunning) {
        return;
    }
    if (fileIndex <= 0 || fileIndex > static_cast<int>(m_files.size())) {
        m_virusTotalStatus->copy_label("Select a file first.");
        return;
    }

    const TorrentItem::FileInfo file = m_files[fileIndex - 1];
    if (ArchiveInspector::isArchivePath(file.path)) {
        m_virusTotalStatus->copy_label(
            "Use Inspect Archive to choose an .exe inside this archive.");
        return;
    }
    if (!isExecutablePath(file.path)) {
        m_virusTotalStatus->copy_label("Only .exe files can be checked.");
        return;
    }
    if (!file.complete) {
        m_virusTotalStatus->copy_label(
            "Wait until this file finishes downloading before hashing it.");
        return;
    }

    const std::string localPath = resolveLocalPath(file.path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(pathFromUtf8(localPath), ec)) {
        m_virusTotalStatus->copy_label(
            "The downloaded file is missing or is not a regular file. Check the save path.");
        return;
    }

    m_hashRunning = true;
    m_statusSticky = false;
    m_virusTotalButton->deactivate();
    m_virusTotalStatus->copy_label("Computing SHA-256...");
    m_virusTotalStatus->tooltip(nullptr);

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_hashCancel = cancel;
    const std::string displayPath = file.path.empty() ? file.name : file.path;

    try {
        std::thread([cancel, this, localPath, displayPath]() {
            std::string hash;
            std::string error;
            bool ok = false;
            try {
                ok = FileHash::sha256File(localPath, hash, error, cancel.get());
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "unexpected error";
            }

            // Closing the dialog is a cancellation: do not open a browser after
            // the user has moved on.
            if (cancel->load()) {
                return;
            }

            auto* delivery = new HashDelivery;
            delivery->cancel = cancel;
            delivery->target = this;
            delivery->ok = ok;
            delivery->path = displayPath;
            delivery->hash = hash;
            delivery->error = error;
            Fl::awake(&TorrentDetailsDialog::deliverHash, delivery);
        }).detach();
    } catch (const std::exception& e) {
        m_hashRunning = false;
        m_statusSticky = true;
        m_virusTotalStatus->copy_label(
            (std::string("Could not start the hash worker: ") +
             displayText(e.what())).c_str());
        updateVirusTotalControls();
    }
}

void TorrentDetailsDialog::handleHashFinished(bool ok,
                                               const std::string& path,
                                               const std::string& hash,
                                               const std::string& error) {
    m_hashRunning = false;
    updateVirusTotalControls();

    if (!ok) {
        m_statusSticky = true;
        m_virusTotalStatus->copy_label(
            (std::string("Could not read ") + displayText(path) + ": " +
             displayText(error)).c_str());
        return;
    }

    const std::string url = VirusTotal::fileReportUrl(hash);
    if (url.empty()) {
        m_statusSticky = true;
        m_virusTotalStatus->copy_label("Could not build a valid VirusTotal link.");
        return;
    }

    m_virusTotalStatus->copy_label(
        "SHA-256 computed. Opening the VirusTotal report; FTorrent does not read the verdict.");
    m_virusTotalStatus->copy_tooltip(hash.c_str());
    SystemUtils::openUrl(url);
}

std::string TorrentDetailsDialog::formatLabel(const std::string& label, const std::string& value) {
    return label + ": " + value;
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

void TorrentDetailsDialog::onFileSelected(Fl_Widget* w, void* data) {
    (void)w;
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    if (dlg->m_rebuildingFiles) {
        return;
    }
    dlg->m_statusSticky = false;
    dlg->updateVirusTotalControls();
}

void TorrentDetailsDialog::onVirusTotal(Fl_Widget* w, void* data) {
    (void)w;
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    const int value = dlg->m_filesBrowser->value();
    if (value > 0 && value <= static_cast<int>(dlg->m_files.size()) &&
        ArchiveInspector::isArchivePath(dlg->m_files[value - 1].path)) {
        dlg->openArchiveContents(value);
    } else {
        dlg->startVirusTotalCheck(value);
    }
}

void TorrentDetailsDialog::deliverHash(void* data) {
    std::unique_ptr<HashDelivery> delivery(static_cast<HashDelivery*>(data));
    if (!delivery || !delivery->cancel || delivery->cancel->load() || !delivery->target) {
        return;
    }
    delivery->target->handleHashFinished(
        delivery->ok, delivery->path, delivery->hash, delivery->error);
}

void TorrentDetailsDialog::updateTimerCallback(void* data) {
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    
    if (dlg && dlg->shown()) {
        dlg->updateInfo();
        Fl::repeat_timeout(2.0, updateTimerCallback, data);
    }
}
