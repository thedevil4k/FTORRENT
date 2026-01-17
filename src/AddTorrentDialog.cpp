#include "AddTorrentDialog.h"
#include "SettingsManager.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include "PathUtils.h"

AddTorrentDialog::AddTorrentDialog()
    : Fl_Window(500, 520, "Add Torrent")
    , m_okClicked(false)
{
    m_startImmediatelyResult = true;
    m_priority = Priority::Normal;
    
    createUI();
    createButtons();
    
    end();
    set_modal();
    
    loadDefaults();
}

AddTorrentDialog::~AddTorrentDialog() {
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
    m_startImmediately = new Fl_Check_Button(20, y, 200, 25, "Start download immediately");
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
    Fl_Box* filesLabel = new Fl_Box(20, y, 100, 25, "Files in torrent:");
    filesLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 25;

    m_filesBuffer = new Fl_Text_Buffer();
    m_filesDisplay = new Fl_Text_Display(20, y, 460, 120);
    m_filesDisplay->buffer(m_filesBuffer);
    m_filesDisplay->textfont(FL_COURIER);
    m_filesDisplay->textsize(11);
    y += 130;
}

void AddTorrentDialog::createButtons() {
    int btn_y = 470;
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
        // If magnet is provided, we prioritize it or ensure m_torrentPath is clear if we only want one
        if (!m_torrentPath.empty()) {
            fl_message("Both file and magnet provided. Prioritizing the torrent file.");
            m_magnetLink = "";
        }
    }
    
    // Validate save path
    if (strlen(m_savePathInput->value()) == 0) {
        fl_alert("Please select a download directory");
        return false;
    }
    m_savePath = m_savePathInput->value();
    
    // Get other settings
    m_startImmediatelyResult = (m_startImmediately->value() != 0);
    
    switch (m_priorityChoice->value()) {
        case 0: m_priority = Priority::Normal; break;
        case 1: m_priority = Priority::High; break;
        case 2: m_priority = Priority::Low; break;
        default: m_priority = Priority::Normal;
    }
    
    return true;
}

bool AddTorrentDialog::show_modal() {
    m_okClicked = false;
    show();
    
    while (shown()) {
        Fl::wait();
    }
    
    return m_okClicked;
}

// Static callbacks
void AddTorrentDialog::onBrowseFile(Fl_Widget* w, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;
    
    const char* filename = PathUtils::showFileChooser(
        "Select Torrent File",
        "Torrent Files (*.torrent)",
        nullptr,
        Fl_File_Chooser::SINGLE
    );
    
    if (filename) {
        dlg->m_filePathInput->value(filename);
        dlg->updateFileList(filename);
    }
}

void AddTorrentDialog::onBrowseSavePath(Fl_Widget* w, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;
    
    const char* dir = PathUtils::showDirChooser(
        "Select Download Directory",
        dlg->m_savePathInput->value()
    );
    
    if (dir) {
        dlg->m_savePathInput->value(dir);
    }
}

void AddTorrentDialog::onOK(Fl_Widget* w, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;
    
    if (dlg->validate()) {
        dlg->m_okClicked = true;
        dlg->hide();
    }
}

void AddTorrentDialog::onCancel(Fl_Widget* w, void* data) {
    AddTorrentDialog* dlg = (AddTorrentDialog*)data;
    dlg->m_okClicked = false;
    dlg->hide();
}

#include <libtorrent/torrent_info.hpp>
#include "TorrentItem.h"

void AddTorrentDialog::updateFileList(const std::string& torrentPath) {
    if (torrentPath.empty()) {
        m_filesBuffer->text("");
        return;
    }

    try {
        lt::torrent_info info(torrentPath);
        std::ostringstream oss;
        
        oss << std::left << std::setw(40) << "Filename" << " | " << "Size" << "\n";
        oss << std::string(55, '-') << "\n";
        
        auto const& fs = info.files();
        for (int i = 0; i < fs.num_files(); ++i) {
            oss << std::left << std::setw(40) << fs.file_name(lt::file_index_t(i)).to_string().substr(0, 39) << " | "
                << TorrentItem::formatSize(fs.file_size(lt::file_index_t(i))) << "\n";
        }
        
        m_filesBuffer->text(oss.str().c_str());
        
    } catch (const std::exception& e) {
        m_filesBuffer->text(("Error reading torrent file: " + std::string(e.what())).c_str());
    }
}
