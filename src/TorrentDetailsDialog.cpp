#include "TorrentDetailsDialog.h"
#include <FL/Fl.H>
#include <sstream>
#include <iomanip>

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
    
    delete m_trackersBuffer;
    delete m_peersBuffer;
    delete m_filesBuffer;
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
    
    m_filesBuffer = new Fl_Text_Buffer();
    m_filesDisplay = new Fl_Text_Display(20, 50, 560, 380);
    m_filesDisplay->buffer(m_filesBuffer);
    m_filesDisplay->textfont(FL_COURIER);
    m_filesDisplay->textsize(11);
    
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
    auto files = m_torrent->getFiles();
    std::ostringstream oss;
    
    oss << std::left << std::setw(40) << "Filename" << " | " 
        << std::setw(10) << "Size" << " | "
        << "Progress" << "\n";
    oss << std::string(70, '-') << "\n";
    
    for (const auto& f : files) {
        oss << std::left << std::setw(40) << f.name.substr(0, 39) << " | " 
            << std::setw(10) << TorrentItem::formatSize(f.size) << " | "
            << std::fixed << std::setprecision(1) << (f.progress * 100.0) << "%\n";
    }
    
    m_filesBuffer->text(oss.str().c_str());
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
    TorrentDetailsDialog* dlg = (TorrentDetailsDialog*)data;
    dlg->updateInfo();
}

void TorrentDetailsDialog::onClose(Fl_Widget* w, void* data) {
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
