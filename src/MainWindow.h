#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Box.H>
#include <memory>
#include "ToolbarLayout.h"
#include "TorrentManager.h"
#include "TorrentListWidget.h"
#include "PreferencesDialog.h"
#include "AddTorrentDialog.h"
#include "TorrentDetailsDialog.h"
#include "SearchResultsWidget.h"
#include "SearchEngine.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <map>
#include <string>
#include <vector>

// Forward declaration only; the full header is pulled in by MainWindow.cpp.
// Keeps this header free of menu-internals includes on every platform.
struct Fl_Multi_Label;
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
public:    MainWindow(int w, int h, const char* title);
    ~MainWindow();

    static constexpr const char* VERSION = "0.5.0";

    // False once the window is destroyed: worker threads check it before
    // handing results back, so a search in flight cannot touch freed memory.
    static bool isAlive() { return s_alive; }

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
    void toggleCensorship();
    
    // Syncs the censor (eye) button with m_censored / m_showPublicIp
    void updateCensorButtonState();
    
    // Actions
    // True when the user confirmed and the torrent was handed to the session.
    // Callers may ignore the result; search uses it to stay on the results
    // when the dialog is cancelled. prefilledFiles lists the magnet's files in
    // torrent order (empty when unknown): the dialog shows one checkbox each.
    bool showAddTorrentDialog(const std::string& prefilledPath = "", const std::string& prefilledMagnet = "",
                              const std::vector<TorrentFileEntry>& prefilledFiles = {});
    void showCreateTorrentDialog();
    void showPreferencesDialog();
    void showAboutDialog();
    
    // Torrent actions
    void toggleSelectedTorrents();
    void removeSelectedTorrents(bool deleteFiles = false);

    // Waits for every engine-probe worker to finish (in-flight GETs abort at
    // once: s_alive is already false, which is their stop condition).
    // Idempotent: safe to call twice, and the destructor calls it too. main()
    // and the tests call it because neither deletes the window.
    void shutdownEngineProbes();

private:
    // UI Components
    // The bar and the row of buttons are separate widgets. The group is the
    // 44px strip; the pack inside it is 34px, because Fl_Pack stretches its
    // children to ITS OWN height and the buttons have to be the ones that get
    // padding, not the strip.
    Fl_Group* m_toolbar;
    Fl_Pack* m_toolbarPack;
    Fl_Group* m_statusGroup;
    // Elastic spacers at both ends of the pack: their widths are the margins
    // that centre the rest of the bar in the window. Fl_Pack shrinks to its
    // children, so both ends are needed for the block to land in the middle.
    Fl_Box* m_leadSpacer;
    Fl_Box* m_trailSpacer;
    Fl_Button* m_btnAdd;
    Fl_Button* m_btnCreate;
    Fl_Box* m_spacer1;
    Fl_Box* m_spacer2;
    Fl_Button* m_btnPrefs;
    Fl_Button* m_btnTogglePause;
    Fl_Button* m_btnRemove;
    Fl_Button* m_darkModeBtn;
    Fl_Button* m_btnSearch;
    TorrentListWidget* m_torrentList;
    
    // Reachability of each search engine, shown as a dot in the engine
    // dropdown: green = answered, red = unreachable, gray = not checked yet.
    // Green means the host answered, not that its results are good.
    enum class EngineStatus { Unknown, Up, Down };
    // Search view (shown in place of the torrent list)
    Fl_Group* m_searchView;
    Fl_Input* m_searchInput;
    Fl_Button* m_btnSearchGo;
    Fl_Button* m_btnSearchCancel;
    Fl_Choice* m_choiceSearchEngine;
    // Category for the selected engine (only visible when it declares more
    // than one). Width is reclaimed by the query field when hidden.
    Fl_Choice* m_choiceSearchCategory;
    Fl_Box* m_searchStatus;
    SearchResultsWidget* m_searchResults;
    bool m_searching;
    // Set when the user cancels: the worker checks it and stops early
    std::atomic<bool> m_searchCancelled;
    // Incremented on every search. A thread whose number is no longer the
    // current one has been superseded and must not touch the UI state.
    std::atomic<unsigned long> m_searchGeneration;
    
    // Which view is currently visible
    bool m_showSearchView;
    Fl_Box* m_statusBar;
    Fl_Button* m_btnCensor;
    Fl_Button* m_btnLimit;
    Fl_Choice* m_choiceRamMode;
    bool m_limitModerate;
    // The background the limit button had before it ever turned magenta, i.e.
    // the one every other button in the toolbar uses.
    Fl_Color m_limitIdleColor;
    bool m_censored;
    bool m_showPublicIp;
    
    // The theme is not a preference any more. It is true from the moment the
    // window is built and nothing but the toolbar button can change it, so no
    // settings.ini can hand the light theme over to the next launch.
    bool m_darkMode;
    
    // How much room the toolbar had when it was last laid out, as a level of
    // detail. The policy behind the number lives in ToolbarLayout.
    ToolbarLayout::Level m_toolbarLevel;
    // How much spare width the bar was last laid out for. The level alone is
    // not enough to tell whether the layout needs redoing: widening the window
    // inside the same level still has to move the block and re-centre it.
    int m_toolbarSlack;
    
    // False once the window is destroyed: worker threads check it before
    // handing results back, so a search in flight cannot touch freed memory.
    static std::atomic<bool> s_alive;
    
    // Fl::awake() only accepts a plain function pointer, so a worker thread
    // cannot carry its results inside a capturing lambda. They are parked here
    // under a mutex instead, and the callback (which receives "this") picks
    // them up on the UI thread.
    struct PendingSearch {
        std::vector<SearchResult> results;
        std::string engine;
        std::string error;
    };
    // Fl::awake() takes a plain function pointer, so the handlers that bring
    // worker results back to the UI are static members: that way they keep
    // access to the private state they need.
    static void handleSearchBatch(void* data);
    static void handleSearchDone(void* data);
    static void handleStatusRefresh(void* data);
    static void handleSearchStatus(void* data);
    std::mutex m_searchMutex;
    PendingSearch m_pendingSearch;
    // Batches parsed by the search thread, waiting to be painted. Fl::awake
    // only accepts a plain function, so the data travels through here.
    //
    // This is a queue and not a single batch on purpose. Fl::awake() only
    // queues the handler, not the payload, so two batches parsed back to back
    // woke the UI twice but left only the second one in memory: the handler
    // drained an empty slot and the first batch was dropped without a trace.
    // A search of 100 results showed 25 for that reason. Every batch is now
    // parked in order and the handler takes all of them at once.
    std::deque<std::vector<SearchResult>> m_pendingBatch;
    // One-line status posted by the search thread ("Contacting X, page N").
    // Painted on the UI thread so a stall names its culprit instead of
    // leaving a bare "Searching...".
    std::string m_pendingStatus;
    
    // Manager
    TorrentManager* m_manager;
    
    // Dots for the engine dropdown, painted procedurally (no assets, no new
    // dependencies). The pixel buffers must outlive the images, hence members.
    // Attached through Fl_Multi_Label (dot + name), never through
    // Fl_Menu_Item::image(): that call REPLACES the item text (verified at
    // runtime), which is how the dropdown ended up showing dots only.
    static constexpr int kEngineDotPixels = 12;
    unsigned char m_dotGreenPx[kEngineDotPixels * kEngineDotPixels * 4];
    unsigned char m_dotRedPx[kEngineDotPixels * kEngineDotPixels * 4];
    unsigned char m_dotGrayPx[kEngineDotPixels * kEngineDotPixels * 4];
    Fl_Image* m_dotGreen;
    Fl_Image* m_dotRed;
    Fl_Image* m_dotGray;
    // Last known reachability per engine name; absent means not checked yet.
    std::map<std::string, EngineStatus> m_engineStatus;
    // One multi-label per engine menu row, in menu order. Swapping the dot on
    // a status change only rewrites labela (the image), so the name pointer
    // is never at risk; entries are rebuilt together with the menu itself.
    std::vector<Fl_Multi_Label*> m_engineMultis;
    // The reachability probe runs once per session, the first time the search
    // view is opened, so startup and the torrent list never pay for it.
    bool m_engineStatusChecked;
    // Probe workers. Joinable, never detached: a detached probe doing a
    // blocking GET outlives main() and dies inside teardown (curl/WinInet
    // already half gone), which is a segfault at exit. Touched only on the
    // UI thread (launch here, join in shutdownEngineProbes()).
    std::vector<std::thread> m_engineProbes;
    // Tells in-flight probes to abort at once, so the join never waits out a
    // full timeout (matters for the test suite, where s_alive never falls).
    std::atomic<bool> m_probesStop;

    // Icons
    Fl_Image* m_brightIcon;
    Fl_Image* m_darkIcon;
    Fl_Image* m_addIcon;
    Fl_Image* m_createIcon;
    Fl_Image* m_ecoIcon;
    Fl_Image* m_normalIcon;
    Fl_Image* m_turboIcon;
    Fl_Image* m_eyeOpenedIcon;
    Fl_Image* m_eyeClosedIcon;
    // Supplied as PNGs. Unlike the eye icons these never change with the
    // theme, so they are loaded once and never reloaded.
    Fl_Image* m_findIcon;
    Fl_Image* m_limitIcon;
    
    // Latency measurement
    int m_latency = -1;
    int m_latencyTicker = 0;
    
    // Layout constants
    static constexpr int MENU_HEIGHT = 0;
    static constexpr int TOOLBAR_HEIGHT = ToolbarLayout::kBarHeight;
    static constexpr int STATUS_HEIGHT = 25;
    // Search bar sits right under the toolbar; the status line under it
    static constexpr int SEARCH_BAR_HEIGHT = 35;
    static constexpr int SEARCH_STATUS_HEIGHT = 18;
    
    // UI Creation
    void createToolbar();
    void createTorrentList();
    void createStatusBar();
    void createSearchView();
    void layoutSearchView();
    void layoutSearchViewContents(int available);
    void switchView(bool showSearch);
    void startSearch();
    void updateEngineChoice();
    // Engine reachability dots. startEngineStatusCheck() launches one detached
    // probe per enabled engine (HttpClient only, no new libraries) and each
    // result comes back through Fl::awake() to applyEngineStatus(), which runs
    // on the UI thread. engineMenuIndex() maps an engine name to its dropdown
    // row, or -1 when it is not listed.
    void startEngineStatusCheck();
    void applyEngineStatus(const std::string& name, bool up);
    int engineMenuIndex(const std::string& name) const;
    Fl_Image* dotForEngine(const std::string& name) const;
    struct EngineStatusDelivery { MainWindow* win; std::string name; bool up; };
    static void handleEngineStatus(void* data);
    void updateCategoryChoice();
    void setSearchStatus(const std::string& text);
    void onSearchResults(const std::vector<SearchResult>& results,
                         const std::string& engineName,
                         const std::string& error);
    void appendSearchResults(const std::vector<SearchResult>& batch);
    static void onSearchCancelClick(Fl_Widget* w, void* data);
    void loadSearchEngines();
    
    // Responsive layout
    void layoutToolbar(int available);
    void reloadEyeIcons();
    void applyLimitStyle();
    
    // Menu callbacks
    static void menuCallback(Fl_Widget* w, void* data);
    
    // Toolbar button callbacks
    static void onAddTorrent(Fl_Widget* w, void* data);
    static void onCreateTorrent(Fl_Widget* w, void* data);
    static void onTogglePause(Fl_Widget* w, void* data);
    static void onRemove(Fl_Widget* w, void* data);
    static void onPreferences(Fl_Widget* w, void* data);
    static void onToggleTheme(Fl_Widget* w, void* data);
    static void onSearchToggle(Fl_Widget* w, void* data);
    static void onSearchGo(Fl_Widget* w, void* data);
    static void onToggleLimit(Fl_Widget* w, void* data);
    static void onToggleCensorship(Fl_Widget* w, void* data);
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
