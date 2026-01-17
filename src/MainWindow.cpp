#include "MainWindow.h"
#include "CreateTorrentDialog.h"
#include "SettingsManager.h"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Shared_Image.H>
#include <sstream>
#include <cstdint>


MainWindow::MainWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
    , m_toolbar(nullptr)
    , m_darkModeBtn(nullptr)
    , m_torrentList(nullptr)
    , m_statusBar(nullptr)
    , m_manager(nullptr)
{
    // Initialize image support
    fl_register_images();
    
    // Set application icon
    if (Resources::getLogoImage()) {
        icon(Resources::getLogoImage());
    }
    
    // Create UI components
    createToolbar();
    createTorrentList();
    createStatusBar();
    
    applyTheme();
    
    resizable(m_torrentList);
    end();
    
    // Restore window state
    restoreWindowState();
    
    // Set up update timer (1 second)
    Fl::add_timeout(1.0, updateTimerCallback, this);
}

MainWindow::~MainWindow() {
    saveWindowState();
    Fl::remove_timeout(updateTimerCallback, this);
}

// Menu Bar removed

void MainWindow::createToolbar() {
    int y = MENU_HEIGHT;
    m_toolbar = new Fl_Pack(0, y, w(), TOOLBAR_HEIGHT);
    m_toolbar->type(Fl_Pack::HORIZONTAL);
    m_toolbar->spacing(5);
    m_toolbar->begin();
    
    // Add torrent button
    m_btnAdd = new Fl_Button(0, 0, 130, 30, "Add Torrent");
    m_btnAdd->callback(onAddTorrent, this);
    if (Resources::getAddIcon()) {
        m_btnAdd->image(Resources::getAddIcon());
        m_btnAdd->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Create torrent button
    m_btnCreate = new Fl_Button(0, 0, 130, 30, "Create Torrent");
    m_btnCreate->callback(onCreateTorrent, this);
    if (Resources::getAddIcon()) { // Using same icon for now
        m_btnCreate->image(Resources::getAddIcon());
        m_btnCreate->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Spacer
    Fl_Box* spacer1 = new Fl_Box(0, 0, 20, 30);
    spacer1->box(FL_NO_BOX);
    
    // Pause button
    m_btnPause = new Fl_Button(0, 0, 80, 30, "Pause");
    m_btnPause->callback(onPause, this);
    if (Resources::getPauseIcon()) {
        m_btnPause->image(Resources::getPauseIcon());
        m_btnPause->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Resume button
    m_btnResume = new Fl_Button(0, 0, 90, 30, "Resume");
    m_btnResume->callback(onResume, this);
    if (Resources::getPlayIcon()) {
        m_btnResume->image(Resources::getPlayIcon());
        m_btnResume->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Remove button
    m_btnRemove = new Fl_Button(0, 0, 90, 30, "Remove");
    m_btnRemove->callback(onRemove, this);
    if (Resources::getRemoveIcon()) {
        m_btnRemove->image(Resources::getRemoveIcon());
        m_btnRemove->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Spacer
    Fl_Box* spacer2 = new Fl_Box(0, 0, 20, 30);
    spacer2->box(FL_NO_BOX);
    
    // Preferences button
    Fl_Button* btnPrefs = new Fl_Button(0, 0, 120, 30, "Preferences");
    btnPrefs->callback(onPreferences, this);
    if (Resources::getSettingsIcon()) {
        btnPrefs->image(Resources::getSettingsIcon());
        btnPrefs->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Theme button
    m_darkModeBtn = new Fl_Button(0, 0, 110, 30, "Dark Mode");
    m_darkModeBtn->callback(onToggleTheme, this);
    
    m_toolbar->end();
}

void MainWindow::createTorrentList() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int list_h = h() - y - STATUS_HEIGHT;
    
    m_torrentList = new TorrentListWidget(0, y, w(), list_h);
}

void MainWindow::createStatusBar() {
    int y = h() - STATUS_HEIGHT;
    
    m_statusBar = new Fl_Box(0, y, w(), STATUS_HEIGHT);
    m_statusBar->box(FL_DOWN_BOX);
    m_statusBar->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_statusBar->label("Ready");
}

void MainWindow::setTorrentManager(TorrentManager* manager) {
    m_manager = manager;
    
    if (m_manager) {
        // Set up callbacks
        m_manager->setOnTorrentAdded([this](TorrentItem* item) {
            if (m_torrentList) {
                m_torrentList->addTorrent(item);
            }
        });
        
        m_manager->setOnTorrentRemoved([this](const std::string& hash) {
            if (m_torrentList) {
                m_torrentList->removeTorrent(hash);
            }
        });
        
        m_manager->setOnTorrentUpdated([this](TorrentItem* item) {
            if (m_torrentList) {
                m_torrentList->updateTorrent(item);
            }
        });
        
        m_manager->setOnStatsUpdated([this]() {
            updateStatusBar();
        });
        
        m_manager->setOnError([](const std::string& error) {
            fl_alert("Error: %s", error.c_str());
        });
        
        // Initial update
        updateUI();
    }
}

void MainWindow::updateUI() {
    if (!m_manager || !m_torrentList) {
        return;
    }
    
    // Update torrent list
    auto torrents = m_manager->getAllTorrents();
    m_torrentList->setTorrents(torrents);
    
    // Update status bar
    updateStatusBar();

    // Update toolbar
    updateToolbar();
}

void MainWindow::updateStatusBar() {
    if (!m_statusBar || !m_manager) {
        return;
    }
    
    std::string status = formatStatusBar();
    m_statusBar->copy_label(status.c_str());
}

void MainWindow::updateToolbar() {
    if (!m_torrentList) return;
    
    bool hasSelection = m_torrentList->hasSelection();
    
    if (m_btnPause) hasSelection ? m_btnPause->activate() : m_btnPause->deactivate();
    if (m_btnResume) hasSelection ? m_btnResume->activate() : m_btnResume->deactivate();
    if (m_btnRemove) hasSelection ? m_btnRemove->activate() : m_btnRemove->deactivate();
}

void MainWindow::applyTheme() {
    bool darkMode = SettingsManager::instance().getDarkMode();
    
    if (darkMode) {
        // Dark Mode Colors
        Fl::background(0, 0, 0);       // Fondo negro absoluto para la ventana
        Fl::background2(20, 20, 20);   // Casi negro para las listas
        Fl::foreground(255, 255, 255); // Blanco puro para el texto
        
        // Colores de selección
        Fl::set_color(FL_SELECTION_COLOR, 40, 80, 160);
        
        // Colores para el efecto 3D de los botones (bordes)
        // Redefinimos los grises que usa FLTK para el sombreado de cajas
        Fl::set_color(FL_GRAY0, 70, 70, 70);   // Un gris medio-oscuro para los bordes
        Fl::set_color(FL_DARK3, 40, 40, 40);   // Gris más oscuro para sombras profundas
        Fl::set_color(FL_LIGHT3, 100, 100, 100); // Gris más claro para brillos
        
        if (m_darkModeBtn) m_darkModeBtn->label("Light Mode");
    } else {
        // Light Mode Colors
        Fl::background(225, 225, 225); // Gris claro estándar (tipo Windows clásico)
        Fl::background2(255, 255, 255);
        Fl::foreground(0, 0, 0);
        
        // Reset colors to defaults
        Fl::set_color(FL_SELECTION_COLOR, 0, 81, 255); // Azul estándar
        
        if (m_darkModeBtn) m_darkModeBtn->label("Dark Mode");
    }
    
    // Aplicar a todos los widgets existentes
    for (int i = 0; i < children(); i++) {
        child(i)->redraw();
    }
    
    // Forzamos el rediseño de la ventana y sus hijos
    redraw();
    if (m_torrentList) {
        m_torrentList->redraw();
    }
}

void MainWindow::toggleDarkMode() {
    bool current = SettingsManager::instance().getDarkMode();
    SettingsManager::instance().setDarkMode(!current);
    SettingsManager::instance().save();
    applyTheme();
}

std::string MainWindow::formatStatusBar() const {
    if (!m_manager) {
        return "Not initialized";
    }
    
    int totalTorrents = m_manager->getTorrentCount();
    int activeTorrents = m_manager->getActiveTorrentsCount();
    int downRate = m_manager->getTotalDownloadRate();
    int upRate = m_manager->getTotalUploadRate();
    
    std::ostringstream oss;
    oss << "Torrents: " << totalTorrents 
        << " (Active: " << activeTorrents << ")  |  ";
    
    if (downRate > 0 || upRate > 0) {
        oss << "↓ " << TorrentItem::formatSpeed(downRate) << "  "
            << "↑ " << TorrentItem::formatSpeed(upRate);
    } else {
        oss << "Idle";
    }
    
    return oss.str();
}

void MainWindow::showAddTorrentDialog() {
    AddTorrentDialog* dlg = new AddTorrentDialog();
    
    if (dlg->show_modal() && m_manager) {
        std::string path = dlg->getTorrentPath();
        std::string magnet = dlg->getMagnetLink();
        std::string savePath = dlg->getSavePath();
        
        bool success = false;
        if (!path.empty()) {
            success = m_manager->addTorrentFile(path, savePath);
        } else if (!magnet.empty()) {
            success = m_manager->addMagnetLink(magnet, savePath);
        }
        
        if (success) {
            updateUI();
        } else {
            fl_alert("Failed to add torrent");
        }
    }
    
    delete dlg;
}

void MainWindow::showCreateTorrentDialog() {
    CreateTorrentDialog* dlg = new CreateTorrentDialog();
    dlg->show_modal();
    delete dlg;
}

void MainWindow::showPreferencesDialog() {
    PreferencesDialog* dlg = new PreferencesDialog();
    
    if (dlg->show_modal()) {
        // Settings were saved, might need to apply some changes
        updateUI();
    }
    
    delete dlg;
}

void MainWindow::showAboutDialog() {
    fl_message(
        "FLTorrent v0.1.0\n\n"
        "A lightweight BitTorrent client built with FLTK\n\n"
        "Powered by libtorrent-rasterbar\n\n"
        "© 2026"
    );
}

void MainWindow::pauseSelectedTorrents() {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    for (auto* torrent : selected) {
        m_manager->pauseTorrent(torrent->getHash());
    }
}

void MainWindow::resumeSelectedTorrents() {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    for (auto* torrent : selected) {
        m_manager->resumeTorrent(torrent->getHash());
    }
}

void MainWindow::removeSelectedTorrents(bool deleteFiles) {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    if (selected.empty()) return;
    
    const char* msg = deleteFiles ? 
        "Remove selected torrents and delete files?" :
        "Remove selected torrents?";
    
    if (fl_choice(msg, "Cancel", "Remove", nullptr) == 1) {
        // Collect hashes first to avoid dangling pointers if removals trigger list updates
        std::vector<std::string> hashes;
        for (auto* torrent : selected) {
            if (torrent) {
                hashes.push_back(torrent->getHash());
            }
        }
        
        for (const auto& hash : hashes) {
            m_manager->removeTorrent(hash, deleteFiles);
        }
    }
}

void MainWindow::saveWindowState() {
    auto& settings = SettingsManager::instance();
    settings.setWindowX(x());
    settings.setWindowY(y());
    settings.setWindowWidth(w());
    settings.setWindowHeight(h());
    settings.save();
}

void MainWindow::restoreWindowState() {
    auto& settings = SettingsManager::instance();
    settings.load();
    
    int win_x = settings.getWindowX();
    int win_y = settings.getWindowY();
    int win_w = settings.getWindowWidth();
    int win_h = settings.getWindowHeight();
    
    // Validate and apply
    if (win_w > 400 && win_h > 300) {
        resize(win_x, win_y, win_w, win_h);
    }
}

int MainWindow::handle(int event) {
    if (event == FL_CLOSE) {
        saveWindowState();
    }
    return Fl_Double_Window::handle(event);
}

// Static callbacks
void MainWindow::onAddTorrent(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->showAddTorrentDialog();
}

void MainWindow::onCreateTorrent(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->showCreateTorrentDialog();
}

void MainWindow::onPause(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->pauseSelectedTorrents();
}

void MainWindow::onResume(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->resumeSelectedTorrents();
}

void MainWindow::onRemove(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->removeSelectedTorrents(false);
}

void MainWindow::onPreferences(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->showPreferencesDialog();
}

void MainWindow::onToggleTheme(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->toggleDarkMode();
}

void MainWindow::updateTimerCallback(void* data) {
    MainWindow* win = (MainWindow*)data;
    
    if (win && win->m_manager) {
        win->m_manager->update();
        win->updateToolbar(); // Keep toolbar updated as selection might change or items might disappear
    }
    
    Fl::repeat_timeout(1.0, updateTimerCallback, data);
}
