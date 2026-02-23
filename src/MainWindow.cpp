#include "MainWindow.h"
#include "CreateTorrentDialog.h"
#include "SettingsManager.h"
#include "RemoveConfirmDialog.h"
#include "Resources.h"
#include "SystemUtils.h"
#include "PathUtils.h"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Choice.H>
#include <FL/x.H>
#include <sstream>
#include <cstdint>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif


MainWindow::MainWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
    , m_toolbar(nullptr)
    , m_darkModeBtn(nullptr)
    , m_torrentList(nullptr)
    , m_statusBar(nullptr)
    , m_manager(nullptr)
    , m_brightIcon(nullptr)
    , m_darkIcon(nullptr)
    , m_addIcon(nullptr)
    , m_createIcon(nullptr)
    , m_ecoIcon(nullptr)
    , m_normalIcon(nullptr)
    , m_turboIcon(nullptr)
    , m_limitModerate(false)
{
    // Initialize image support
    fl_register_images();
    
    // Set application icon
    if (Resources::getLogoImage()) {
        icon((const Fl_RGB_Image*)Resources::getLogoImage());
    }
    
    // Create UI components
    // Load theme icons
    std::string appDir = PathUtils::getAppDirPath();
    std::string assetsDir = appDir + "/assets/";
    // Normalize slashes for FLTK image loader
    std::replace(assetsDir.begin(), assetsDir.end(), '\\', '/');

    Fl_PNG_Image* imgTemp;
    
    imgTemp = new Fl_PNG_Image((assetsDir + "bright.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_brightIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }
    
    imgTemp = new Fl_PNG_Image((assetsDir + "dark.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_darkIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }
    
    imgTemp = new Fl_PNG_Image((assetsDir + "addtorrent.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_addIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }

    imgTemp = new Fl_PNG_Image((assetsDir + "createtorrent.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_createIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }

    imgTemp = new Fl_PNG_Image((assetsDir + "eco.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_ecoIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }

    imgTemp = new Fl_PNG_Image((assetsDir + "default.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_normalIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }

    imgTemp = new Fl_PNG_Image((assetsDir + "turbo.png").c_str());
    if (imgTemp->d() == 0) { // Failed
        delete imgTemp;
    } else {
        m_turboIcon = imgTemp->copy(20, 20);
        delete imgTemp;
    }

    createToolbar();
    createTorrentList();
    createStatusBar();
    
    applyTheme();
    
    resizable(m_torrentList);
    end();
    
    // Restore window state
    restoreWindowState();
    
    // Set up update timer (100ms for faster alert processing)
    Fl::add_timeout(0.1, updateTimerCallback, this);

#ifdef _WIN32
    Fl::add_handler(win_event_handler);
#endif
}

MainWindow::~MainWindow() {
#ifdef _WIN32
    removeTrayIcon();
#endif
    delete m_brightIcon;
    delete m_darkIcon;
    delete m_addIcon;
    delete m_createIcon;
    delete m_ecoIcon;
    delete m_normalIcon;
    delete m_turboIcon;
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
    m_btnAdd = new Fl_Button(0, 0, 130, 30, " Add Torrent");
    m_btnAdd->box(FL_FLAT_BOX);
    m_btnAdd->callback(onAddTorrent, this);
    if (m_addIcon) {
        m_btnAdd->image(m_addIcon);
        m_btnAdd->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Create torrent button
    m_btnCreate = new Fl_Button(0, 0, 130, 30, " Create Torrent");
    m_btnCreate->box(FL_FLAT_BOX);
    m_btnCreate->callback(onCreateTorrent, this);
    if (m_createIcon) { 
        m_btnCreate->image(m_createIcon);
        m_btnCreate->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Spacer
    Fl_Box* spacer1 = new Fl_Box(0, 0, 20, 30);
    spacer1->box(FL_NO_BOX);
    
    // Toggle Pause/Resume button
    m_btnTogglePause = new Fl_Button(0, 0, 40, 30);
    m_btnTogglePause->box(FL_FLAT_BOX);
    m_btnTogglePause->tooltip("Pause/Resume");
    m_btnTogglePause->callback(onTogglePause, this);
    if (Resources::getPauseIcon()) {
        m_btnTogglePause->image(Resources::getPauseIcon());
    }
    
    // Remove button
    m_btnRemove = new Fl_Button(0, 0, 40, 30);
    m_btnRemove->box(FL_FLAT_BOX);
    m_btnRemove->tooltip("Remove Torrent");
    m_btnRemove->callback(onRemove, this);
    if (Resources::getRemoveIcon()) {
        m_btnRemove->image(Resources::getRemoveIcon());
    }
    
    // Spacer
    Fl_Box* spacer2 = new Fl_Box(0, 0, 20, 30);
    spacer2->box(FL_NO_BOX);
    
    // Preferences button
    Fl_Button* btnPrefs = new Fl_Button(0, 0, 120, 30, "Preferences");
    btnPrefs->box(FL_FLAT_BOX);
    btnPrefs->callback(onPreferences, this);
    if (Resources::getSettingsIcon()) {
        btnPrefs->image(Resources::getSettingsIcon());
        btnPrefs->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Theme button
    m_darkModeBtn = new Fl_Button(0, 0, 30, 30);
    m_darkModeBtn->box(FL_FLAT_BOX);
    m_darkModeBtn->tooltip("Switch Theme");
    m_darkModeBtn->callback(onToggleTheme, this);

    // Spacer
    Fl_Box* spacer3 = new Fl_Box(0, 0, 40, 30);
    spacer3->box(FL_NO_BOX);

    // Dynamic RAM Mode Selector
    m_choiceRamMode = new Fl_Choice(0, 0, 50, 30);
    // Add items with icons if available
    int idx;
    
    idx = m_choiceRamMode->add("  ECO");
    if (m_ecoIcon) {
        // We need to access the menu item directly to set the image
        Fl_Menu_Item* item = const_cast<Fl_Menu_Item*>(m_choiceRamMode->menu() + idx);
        item->image(m_ecoIcon);
    }
    
    idx = m_choiceRamMode->add("  NORMAL");
    if (m_normalIcon) {
        Fl_Menu_Item* item = const_cast<Fl_Menu_Item*>(m_choiceRamMode->menu() + idx);
        item->image(m_normalIcon);
    }
    
    idx = m_choiceRamMode->add("  TURBO");
    if (m_turboIcon) {
        Fl_Menu_Item* item = const_cast<Fl_Menu_Item*>(m_choiceRamMode->menu() + idx);
        item->image(m_turboIcon);
    }
    
    m_choiceRamMode->box(FL_FLAT_BOX);
    m_choiceRamMode->tooltip("RAM Usage Mode: ECO (Zero Buffer), Normal (Balanced), TURBO (Max Buffer)");
    m_choiceRamMode->callback(onRamModeChanged, this);
    m_choiceRamMode->value(SettingsManager::instance().getRamMode());

    // Toggle Network Limit button
    m_btnLimit = new Fl_Button(0, 0, 150, 30, "LIMIT NET SPEED: OFF");
    m_btnLimit->box(FL_FLAT_BOX);
    m_btnLimit->tooltip("Limit network speed");
    m_btnLimit->callback(onToggleLimit, this);
    
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
    
    // Register drag-and-drop callback on the torrent list (cross-platform).
    // On Linux: FLTK DnD events inside TorrentListWidget fire this.
    // On Windows: WM_DROPFILES in win_event_handler also calls addTorrentFile directly.
    if (m_torrentList) {
        m_torrentList->setOnDropCallback([this](const std::string& path) {
            if (!m_manager) return;
            std::string savePath = SettingsManager::instance().getDefaultSavePath();
            bool ok = m_manager->addTorrentFile(path, savePath);
            if (ok) {
                updateUI();
            } else {
                fl_alert("Failed to add torrent: %s", path.c_str());
            }
        });
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
    
    if (m_btnTogglePause) {
        if (hasSelection) {
            m_btnTogglePause->activate();
            
            // Check state of first selected item to determine button state
            bool isPaused = false;
            auto selected = m_torrentList->getSelectedTorrents();
            if (!selected.empty()) {
                isPaused = (selected[0]->getState() == TorrentItem::State::Paused);
            }
            
            if (isPaused) {
                m_btnTogglePause->tooltip("Resume");
                if (Resources::getPlayIcon()) m_btnTogglePause->image(Resources::getPlayIcon());
            } else {
                m_btnTogglePause->tooltip("Pause");
                if (Resources::getPauseIcon()) m_btnTogglePause->image(Resources::getPauseIcon());
            }
        } else {
            m_btnTogglePause->deactivate();
            m_btnTogglePause->tooltip("Pause");
            if (Resources::getPauseIcon()) m_btnTogglePause->image(Resources::getPauseIcon());
        }
        m_btnTogglePause->redraw();
    }
    
    if (m_btnRemove) hasSelection ? m_btnRemove->activate() : m_btnRemove->deactivate();
}

void MainWindow::applyTheme() {
    bool darkMode = SettingsManager::instance().getDarkMode();
    
    if (darkMode) {
        // Dark Mode Colors
        Fl::background(0, 0, 0);       // Absolute black background for window
        Fl::background2(20, 20, 20);   // Near black for lists
        Fl::foreground(255, 255, 255); // Pure white for text
        
        // Selection colors
        Fl::set_color(FL_SELECTION_COLOR, 40, 80, 160);
        
        // 3D effect colors (redefining grays for borders)
        Fl::set_color(FL_GRAY0, 70, 70, 70);   // Dark-mid gray for borders
        Fl::set_color(FL_DARK3, 40, 40, 40);   // Darker gray for deep shadows
        Fl::set_color(FL_LIGHT3, 100, 100, 100); // Lighter gray for highlights
        
        if (m_darkModeBtn) {
            m_darkModeBtn->label(nullptr);
            if (m_darkIcon) m_darkModeBtn->image(m_darkIcon);
            else m_darkModeBtn->label("Light Mode");
        }
    } else {
        // Light Mode Colors
        Fl::background(225, 225, 225); // Standard light gray
        Fl::background2(255, 255, 255);
        Fl::foreground(0, 0, 0);
        
        // Reset colors to defaults
        Fl::set_color(FL_SELECTION_COLOR, 0, 81, 255); // Standard Blue
        
        if (m_darkModeBtn) {
            m_darkModeBtn->label(nullptr);
            if (m_brightIcon) m_darkModeBtn->image(m_brightIcon);
            else m_darkModeBtn->label("Dark Mode");
        }
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
    
    if (m_btnLimit) {
        if (darkMode) {
            m_btnLimit->color(fl_rgb_color(30, 30, 30));
            m_btnLimit->labelcolor(FL_WHITE);
        } else {
            m_btnLimit->color(fl_rgb_color(220, 220, 220));
            m_btnLimit->labelcolor(FL_BLACK);
        }
    }

    if (m_choiceRamMode) {
        if (darkMode) {
            m_choiceRamMode->color(fl_rgb_color(30, 30, 30));
            m_choiceRamMode->labelcolor(FL_WHITE);
            m_choiceRamMode->textcolor(FL_WHITE);
        } else {
            m_choiceRamMode->color(fl_rgb_color(220, 220, 220));
            m_choiceRamMode->labelcolor(FL_BLACK);
            m_choiceRamMode->textcolor(FL_BLACK);
        }
    }
}

void MainWindow::toggleDarkMode() {
    bool current = SettingsManager::instance().getDarkMode();
    SettingsManager::instance().setDarkMode(!current);
    SettingsManager::instance().save();
    applyTheme();
}

void MainWindow::toggleNetworkLimit() {
    m_limitModerate = !m_limitModerate;
    
    if (m_btnLimit) {
        m_btnLimit->label(m_limitModerate ? "LIMIT NET SPEED: 50%" : "LIMIT NET SPEED: OFF");
        m_btnLimit->redraw();
    }
    
    if (m_manager) {
        if (m_limitModerate) {
            // Half the speed of what is in settings (or a default if 0)
            int maxDown = SettingsManager::instance().getMaxDownloadRate();
            int maxUp = SettingsManager::instance().getMaxUploadRate();
            
            // If settings say unlimited (0), we pick a reasonable high value to halve, 
            // but usually limit means some fixed restriction.
            // Let's assume moderate is halving the current settings, 
            // and if they are 0, we set a specific moderate limit (e.g. 500 KB/s down, 100 KB/s up)
            int limitDown = (maxDown > 0) ? maxDown / 2 : 1000; 
            int limitUp = (maxUp > 0) ? maxUp / 2 : 200;
            
            m_manager->setRateLimits(limitDown, limitUp);
        } else {
            // Restore from settings
            m_manager->setRateLimits(
                SettingsManager::instance().getMaxDownloadRate(),
                SettingsManager::instance().getMaxUploadRate()
            );
        }
    }
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
    
    // Add RAM usage
    oss << "  |  RAM: " << SystemUtils::getRamUsage();
    
    // Add Application Version
    oss << "  |  FTORRENT " << VERSION;
    
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
        "FTorrent v0.1.0\n\n"
        "A lightweight BitTorrent client built with FLTK\n\n"
        "Powered by libtorrent-rasterbar\n\n"
        "© 2026"
    );
}

void MainWindow::toggleSelectedTorrents() {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    if (selected.empty()) return;

    // Determine action based on the *first* selected item
    bool shouldResume = (selected[0]->getState() == TorrentItem::State::Paused);

    for (auto* torrent : selected) {
        if (shouldResume) {
            m_manager->resumeTorrent(torrent->getHash());
        } else {
            m_manager->pauseTorrent(torrent->getHash());
        }
    }
    
    updateUI();
}

void MainWindow::removeSelectedTorrents(bool deleteFiles) {
    if (!m_manager || !m_torrentList) return;
    
    auto selected = m_torrentList->getSelectedTorrents();
    if (selected.empty()) return;
    
    RemoveConfirmDialog* dlg = new RemoveConfirmDialog(selected.size());
    int result = dlg->show_modal();
    bool shouldDeleteFiles = dlg->shouldDeleteFiles();
    delete dlg;
    
    if (result == 1) { // Removed
        // Collect hashes first to avoid dangling pointers if removals trigger list updates
        std::vector<std::string> hashes;
        for (auto* torrent : selected) {
            if (torrent) {
                hashes.push_back(torrent->getHash());
            }
        }
        
        for (const auto& hash : hashes) {
            m_manager->removeTorrent(hash, shouldDeleteFiles);
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

void MainWindow::show() {
    Fl_Double_Window::show();
#ifdef _WIN32
    setupTrayIcon();
    // Allow this window to receive files dropped from Windows Explorer
    DragAcceptFiles(fl_xid(this), TRUE);
#endif
}

int MainWindow::handle(int event) {
    if (event == FL_CLOSE) {
        saveWindowState();
#ifdef _WIN32
        auto& settings = SettingsManager::instance();
        if (settings.getMinimizeToTray()) {
            hide();
            return 1;
        }
#endif
    }
    return Fl_Double_Window::handle(event);
}

void MainWindow::resize(int x, int y, int w, int h) {
    Fl_Double_Window::resize(x, y, w, h);
    
    // Status bar is handled by FLTK if resizable is set.
    // Dashboard widgets in toolbar are positioned by Fl_Pack.
}

#ifdef _WIN32
void MainWindow::setupTrayIcon() {
    if (m_trayHasIcon) return;

    HWND hwnd = fl_xid(this);
    if (!hwnd) return;

    // Try to get the window icon set by FLTK icon() call
    HICON hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);
    
    // Fallback to application icon
    if (!hIcon) {
        hIcon = LoadIcon(GetModuleHandle(NULL), IDI_APPLICATION);
    }
    if (!hIcon) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_MESSAGE;
    nid.hIcon = hIcon;
    
    // Set tooltip
    strncpy(nid.szTip, "FTorrent", sizeof(nid.szTip) - 1);
    nid.szTip[sizeof(nid.szTip) - 1] = '\0';

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        m_trayHasIcon = true;
    }
}

void MainWindow::removeTrayIcon() {
    if (!m_trayHasIcon) return;

    HWND hwnd = fl_xid(this);
    if (!hwnd) return;

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;

    Shell_NotifyIcon(NIM_DELETE, &nid);
    m_trayHasIcon = false;
}

int MainWindow::win_event_handler(int event) {
    extern MSG fl_msg;

    // ── WM_DROPFILES: user dropped files from Windows Explorer ────────────
    if (fl_msg.message == WM_DROPFILES) {
        MainWindow* win = (MainWindow*)Fl::first_window();
        if (win && win->m_manager) {
            HDROP hDrop = (HDROP)fl_msg.wParam;
            UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                char buf[MAX_PATH] = {};
                DragQueryFileA(hDrop, i, buf, MAX_PATH);
                std::string path(buf);
                // Only process .torrent files
                if (path.size() > 8 &&
                    path.substr(path.size() - 8) == ".torrent") {
                    std::string savePath =
                        SettingsManager::instance().getDefaultSavePath();
                    bool ok = win->m_manager->addTorrentFile(path, savePath);
                    if (ok) {
                        win->updateUI();
                    } else {
                        fl_alert("Failed to add torrent: %s", path.c_str());
                    }
                }
            }
            DragFinish(hDrop);
        }
        return 1;
    }
    // ─────────────────────────────────────────────────────────────────────

    if (fl_msg.message == WM_TRAY_MESSAGE) {
        if (LOWORD(fl_msg.lParam) == WM_LBUTTONDBLCLK || LOWORD(fl_msg.lParam) == WM_LBUTTONUP) {
            // Show/Restore window
            MainWindow* win = (MainWindow*)Fl::first_window();
            if (win) {
                if (win->visible()) {
                    win->hide();
                } else {
                    win->show();
                    // Bring to front
                    SetForegroundWindow(fl_xid(win));
                }
            }
            return 1;
        } else if (LOWORD(fl_msg.lParam) == WM_RBUTTONUP) {
            // Show a simple context menu
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, "Open FTorrent");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, 3, "Pause All");
            AppendMenu(hMenu, MF_STRING, 4, "Resume All");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, 2, "Exit");
            
            POINT pt;
            GetCursorPos(&pt);
            
            // SetForegroundWindow is necessary for the menu to close when clicking away
            SetForegroundWindow(fl_msg.hwnd);
            
            int id = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, fl_msg.hwnd, NULL);
            DestroyMenu(hMenu);
            
            MainWindow* win = (MainWindow*)Fl::first_window();
            if (!win) return 0;

            if (id == 1) {
                win->show();
                SetForegroundWindow(fl_xid(win));
            } else if (id == 2) {
                // Actual exit from program
                win->removeTrayIcon();
                // Close the session and exit
                exit(0); 
            } else if (id == 3) {
                // Pause all (Optional: could be implemented in manager)
                if (win->m_manager) {
                    for (auto* it : win->m_manager->getAllTorrents()) {
                        win->m_manager->pauseTorrent(it->getHash());
                    }
                }
            } else if (id == 4) {
                // Resume all
                if (win->m_manager) {
                    for (auto* it : win->m_manager->getAllTorrents()) {
                        win->m_manager->resumeTorrent(it->getHash());
                    }
                }
            }
            return 1;
        }
    }
    return 0;
}
#endif

// Static callbacks
void MainWindow::onAddTorrent(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->showAddTorrentDialog();
}

void MainWindow::onCreateTorrent(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->showCreateTorrentDialog();
}

void MainWindow::onTogglePause(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->toggleSelectedTorrents();
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

void MainWindow::onToggleLimit(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->toggleNetworkLimit();
}

void MainWindow::onRamModeChanged(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (win && win->m_choiceRamMode && win->m_manager) {
        int mode = win->m_choiceRamMode->value();
        SettingsManager::instance().setRamMode(mode);
        SettingsManager::instance().save();
        win->m_manager->setRamMode(mode);
    }
}

void MainWindow::updateTimerCallback(void* data) {
    MainWindow* win = (MainWindow*)data;
    
    if (win && win->m_manager) {
        win->m_manager->update();
        win->updateToolbar(); // Keep toolbar updated as selection might change or items might disappear

        // En Modo ECO, forzamos la liberación de memoria de forma agresiva cada 100ms
        if (SettingsManager::instance().getRamMode() == 0) {
            SystemUtils::releaseMemory();
        }
    }
    
    Fl::repeat_timeout(0.1, updateTimerCallback, data);
}
