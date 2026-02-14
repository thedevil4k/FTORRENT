#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Box.H>
#include <memory>
#include "TorrentManager.h"
#include "TorrentListWidget.h"
#include "PreferencesDialog.h"
#include "AddTorrentDialog.h"
#include "TorrentDetailsDialog.h"
#ifdef _WIN32
#include <shellapi.h>
#define WM_TRAY_MESSAGE (WM_USER + 1)
#endif

/**
 * @brief FTorrent main window
 * 
 * Contains the menu, toolbar, torrent list and status bar.
 * Inspired by qBittorrent/Transmission.
 */
class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char* title);
    ~MainWindow();

    // Window management
    void show();
    int handle(int event) override;
    void resize(int x, int y, int w, int h) override;
    
    // TorrentManager integration
    void setTorrentManager(TorrentManager* manager);
    
    // Update UI
    void updateUI();
    void updateStatusBar();
    void updateToolbar();
    
    // Theme
    void applyTheme();
    void toggleDarkMode();
    void toggleNetworkLimit();
    
    // Actions
    void showAddTorrentDialog();
    void showCreateTorrentDialog();
    void showPreferencesDialog();
    void showAboutDialog();
    
    // Torrent actions
    void toggleSelectedTorrents();
    void removeSelectedTorrents(bool deleteFiles = false);
    
private:
    // UI Components
    Fl_Pack* m_toolbar;
    Fl_Button* m_btnAdd;
    Fl_Button* m_btnCreate;
    Fl_Button* m_btnTogglePause;
    Fl_Button* m_btnRemove;
    Fl_Button* m_darkModeBtn;
    TorrentListWidget* m_torrentList;
    Fl_Box* m_statusBar;
    Fl_Button* m_btnLimit;
    Fl_Choice* m_choiceRamMode;
    bool m_limitModerate;
    
    // Manager
    TorrentManager* m_manager;
    
    // Icons
    Fl_RGB_Image* m_brightIcon;
    Fl_RGB_Image* m_darkIcon;
    Fl_RGB_Image* m_addIcon;
    Fl_RGB_Image* m_createIcon;
    Fl_RGB_Image* m_ecoIcon;
    Fl_RGB_Image* m_normalIcon;
    Fl_RGB_Image* m_turboIcon;
    
    // Layout constants
    static constexpr int MENU_HEIGHT = 0;
    static constexpr int TOOLBAR_HEIGHT = 40;
    static constexpr int STATUS_HEIGHT = 25;
    
    // UI Creation
    void createToolbar();
    void createTorrentList();
    void createStatusBar();
    
    // Menu callbacks
    static void menuCallback(Fl_Widget* w, void* data);
    
    // Toolbar button callbacks
    static void onAddTorrent(Fl_Widget* w, void* data);
    static void onCreateTorrent(Fl_Widget* w, void* data);
    static void onTogglePause(Fl_Widget* w, void* data);
    static void onRemove(Fl_Widget* w, void* data);
    static void onPreferences(Fl_Widget* w, void* data);
    static void onToggleTheme(Fl_Widget* w, void* data);
    static void onToggleLimit(Fl_Widget* w, void* data);
    static void onRamModeChanged(Fl_Widget* w, void* data);
    
    // Update timer
    static void updateTimerCallback(void* data);
    
    // Helper methods
    void saveWindowState();
    void restoreWindowState();
    std::string formatStatusBar() const;
    std::string getRamUsage() const;

#ifdef _WIN32
    void setupTrayIcon();
    void removeTrayIcon();
    bool m_trayHasIcon = false;
    static int win_event_handler(int event);
#endif
};

#endif // MAINWINDOW_H
