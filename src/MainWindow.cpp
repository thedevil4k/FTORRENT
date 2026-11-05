#include "MainWindow.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "SystemUtils.h"
#include "AssetLoader.h"
#include "ToolbarLayout.h"
#include "NetworkMonitorDialog.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/x.H>
#include <FL/fl_ask.H>
#include <thread>

std::atomic<bool> MainWindow::s_alive{true};

MainWindow::MainWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
    , m_toolbar(nullptr)
    , m_toolbarPack(nullptr)
    , m_statusGroup(nullptr)
    , m_leadSpacer(nullptr)
    , m_trailSpacer(nullptr)
    , m_darkModeBtn(nullptr)
    , m_torrentList(nullptr)
    , m_searchView(nullptr)
    , m_searchInput(nullptr)
    , m_btnSearchGo(nullptr)
    , m_btnSearchCancel(nullptr)
    , m_choiceSearchEngine(nullptr)
    , m_choiceSearchCategory(nullptr)
    , m_searchStatus(nullptr)
    , m_searchResults(nullptr)
    , m_searching(false)
    , m_searchCancelled(false)
    , m_searchGeneration(0)
    , m_showSearchView(false)
    , m_statusBar(nullptr)
    // From here the list follows the declaration order in MainWindow.h on
    // purpose. C++ initialises members in that order whatever this list says,
    // so a list that disagrees is a trap waiting for the first initialiser that
    // reads another member.
    , m_limitModerate(false)
    , m_limitIdleColor(FL_BACKGROUND_COLOR)
    , m_censored(false)
    , m_showPublicIp(false)
    , m_darkMode(true)
    , m_toolbarLevel(ToolbarLayout::ICONS)
    , m_toolbarSlack(-1)
    , m_manager(nullptr)
    , m_engineStatusChecked(false)
    , m_probesStop(false)
    , m_brightIcon(nullptr)
    , m_darkIcon(nullptr)
    , m_addIcon(nullptr)
    , m_createIcon(nullptr)
    , m_ecoIcon(nullptr)
    , m_normalIcon(nullptr)
    , m_turboIcon(nullptr)
    , m_eyeOpenedIcon(nullptr)
    , m_eyeClosedIcon(nullptr)
    , m_findIcon(nullptr)
    , m_limitIcon(nullptr)
{
    // Initialize image support
    fl_register_images();
    
    // Set application icon
    if (Resources::getLogoImage()) {
        icon((const Fl_RGB_Image*)Resources::getLogoImage());
    }
    
    // Every icon in the window comes from AssetLoader, which owns where the
    // assets live and how a missing one is reported. A nullptr here just means
    // the PNG is not installed: the widget keeps the fallback it already had.
    m_brightIcon     = AssetLoader::load("bright.png", ToolbarLayout::kIconSize);
    m_darkIcon       = AssetLoader::load("dark.png", ToolbarLayout::kIconSize);
    m_addIcon        = AssetLoader::load("addtorrent.png", ToolbarLayout::kIconSize);
    m_createIcon     = AssetLoader::load("createtorrent.png", ToolbarLayout::kIconSize);
    m_ecoIcon        = AssetLoader::load("eco.png", ToolbarLayout::kIconSize);
    m_normalIcon     = AssetLoader::load("default.png", ToolbarLayout::kIconSize);
    m_turboIcon      = AssetLoader::load("turbo.png", ToolbarLayout::kIconSize);
    m_findIcon       = AssetLoader::load("find.png", ToolbarLayout::kIconSize);
    // The bolt gets a bigger box than the magnifier: it is the one icon in the
    // bar that is a bare silhouette, so it needs the extra pixels to read.
    m_limitIcon      = AssetLoader::load("limit_network_speed.png", ToolbarLayout::kIconSize);

    // Nothing to build for the engine status rows here: they are plain text
    // ("● Name" + row color), painted on demand in updateEngineChoice().

    // Censorship state is the persisted setting; the eye icons are the only
    // images that change with the theme, so they get their own reload.
    m_censored = SettingsManager::instance().getIpCensored();
    m_showPublicIp = SettingsManager::instance().getShowPublicIp();
    reloadEyeIcons();

    createToolbar();
    createTorrentList();
    createSearchView();
    loadSearchEngines();
    createStatusBar();
    
    applyTheme();
    
    resizable(m_torrentList);
// Minimum size so the toolbar and the torrent list stay usable.
// 720px is enough for the icon-only toolbar level.
size_range(720, 480);
    end();
    
    // Restore window state
    restoreWindowState();
    
    // Set up update timer (100ms for faster alert processing)
    m_latency = -2; // Initial state: "measuring..."
    m_latencyTicker = 10; // Trigger measurement almost immediately
    Fl::add_timeout(0.1, updateTimerCallback, this);

#ifdef _WIN32
    Fl::add_handler(win_event_handler);
#endif
}

MainWindow::~MainWindow() {
    // Stop any worker thread from handing data back to a window that is gone
    s_alive = false;
    // Join the engine probes before anything they could touch (curl, FLTK,
    // this window) starts going away. Idempotent with the explicit call.
    shutdownEngineProbes();
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
    delete m_eyeOpenedIcon;
    delete m_eyeClosedIcon;
    delete m_findIcon;
    delete m_limitIcon;
    saveWindowState();
    Fl::remove_timeout(updateTimerCallback, this);
}

// Menu Bar removed

void MainWindow::createToolbar() {
    int y = MENU_HEIGHT;
    // The strip and the row of buttons are two widgets. Fl_Pack stretches every
    // child to its own height, so giving the pack the BUTTON height is what
    // leaves room above and below the buttons inside the taller bar; making the
    // pack the bar height instead would pin the buttons to the edges again.
    m_toolbar = new Fl_Group(0, y, w(), TOOLBAR_HEIGHT);
    m_toolbar->box(FL_NO_BOX);
    m_toolbar->begin();
    m_toolbarPack = new Fl_Pack(0, y + ToolbarLayout::kBarPadding,
                                w(), ToolbarLayout::kButtonHeight);
    m_toolbarPack->type(Fl_Pack::HORIZONTAL);
    m_toolbarPack->spacing(5);
    m_toolbarPack->begin();
    
    // First child, so everything after it is pushed right and the block ends up
    // centred. Its width is recomputed on every layoutToolbar() call.
    m_leadSpacer = new Fl_Box(0, 0, 0, ToolbarLayout::kButtonHeight);
    m_leadSpacer->box(FL_NO_BOX);
    
    // Add torrent button
    m_btnAdd = new Fl_Button(0, 0, 130, ToolbarLayout::kButtonHeight, " Add Torrent");
    m_btnAdd->box(FL_FLAT_BOX);
    m_btnAdd->callback(onAddTorrent, this);
    m_btnAdd->tooltip("Add Torrent");
    if (m_addIcon) {
        m_btnAdd->image(m_addIcon);
        m_btnAdd->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Create torrent button
    m_btnCreate = new Fl_Button(0, 0, 130, ToolbarLayout::kButtonHeight, " Create Torrent");
    m_btnCreate->box(FL_FLAT_BOX);
    m_btnCreate->callback(onCreateTorrent, this);
    m_btnCreate->tooltip("Create Torrent");
    if (m_createIcon) { 
        m_btnCreate->image(m_createIcon);
        m_btnCreate->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Spacer
    m_spacer1 = new Fl_Box(0, 0, 20, ToolbarLayout::kButtonHeight);
    m_spacer1->box(FL_NO_BOX);
    
    // Toggle Pause/Resume button
    m_btnTogglePause = new Fl_Button(0, 0, ToolbarLayout::kIconButton, ToolbarLayout::kButtonHeight);
    m_btnTogglePause->box(FL_FLAT_BOX);
    m_btnTogglePause->tooltip("Pause/Resume");
    m_btnTogglePause->callback(onTogglePause, this);
    if (Resources::getPauseIcon()) {
        m_btnTogglePause->image(Resources::getPauseIcon());
    }
    
    // Remove button
    m_btnRemove = new Fl_Button(0, 0, ToolbarLayout::kIconButton, ToolbarLayout::kButtonHeight);
    m_btnRemove->box(FL_FLAT_BOX);
    m_btnRemove->tooltip("Remove Torrent");
    m_btnRemove->callback(onRemove, this);
    if (Resources::getRemoveIcon()) {
        m_btnRemove->image(Resources::getRemoveIcon());
    }
    
    // Spacer
    m_spacer2 = new Fl_Box(0, 0, 20, ToolbarLayout::kButtonHeight);
    m_spacer2->box(FL_NO_BOX);
    
    // Preferences button
    m_btnPrefs = new Fl_Button(0, 0, 120, ToolbarLayout::kButtonHeight, "Preferences");
    m_btnPrefs->box(FL_FLAT_BOX);
    m_btnPrefs->callback(onPreferences, this);
    m_btnPrefs->tooltip("Preferences");
    if (Resources::getSettingsIcon()) {
        m_btnPrefs->image(Resources::getSettingsIcon());
        m_btnPrefs->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    }
    
    // Theme button
    m_darkModeBtn = new Fl_Button(0, 0, ToolbarLayout::kThemeButton, ToolbarLayout::kButtonHeight);
    m_darkModeBtn->box(FL_FLAT_BOX);
    m_darkModeBtn->tooltip("Switch Theme");
    m_darkModeBtn->callback(onToggleTheme, this);

    // RAM mode button: one click cycles NORMAL -> TURBO -> ECO, and the button
    // always shows the current mode outright (icon plus name) instead of
    // hiding it inside a dropdown.
    m_btnRamMode = new Fl_Button(0, 0, ToolbarLayout::kRamButton,
                                 ToolbarLayout::kButtonHeight);
    m_btnRamMode->box(FL_FLAT_BOX);
    m_btnRamMode->align(FL_ALIGN_IMAGE_NEXT_TO_TEXT);
    m_btnRamMode->callback(onRamModeCycle, this);
    updateRamModeButton();

    // Toggle Network Limit button. Icon only, at every toolbar level: the bolt
    // is the whole affordance and the state is carried by the magenta colour.
    m_btnLimit = new Fl_Button(0, 0, ToolbarLayout::kIconButton, ToolbarLayout::kButtonHeight);
    m_btnLimit->box(FL_FLAT_BOX);
    m_btnLimit->tooltip("Limit network speed 50%");
    m_btnLimit->callback(onToggleLimit, this);
    if (m_limitIcon) m_btnLimit->image(m_limitIcon);
    // Whatever FLTK handed this button at creation is what its neighbours in
    // the bar still use, so it is the background to go back to when the limit
    // is switched off: hard-coding a grey here is what made this button look
    // lighter than the rest of the toolbar.
    m_limitIdleColor = m_btnLimit->color();
    
    // Network monitor: what the engine is connected to, and what it has
    // rejected. Icon only, like its neighbours.
    m_btnNetwork = new Fl_Button(0, 0, ToolbarLayout::kIconButton,
                                 ToolbarLayout::kButtonHeight);
    m_btnNetwork->box(FL_FLAT_BOX);
    m_btnNetwork->tooltip("Network monitor: connections and engine events");
    m_btnNetwork->callback(onNetworkMonitor, this);
    if (Resources::getNetworkIcon()) {
        m_btnNetwork->image(Resources::getNetworkIcon());
    }

    // Search toggle, last on the right. Fl_Pack lays children out in creation
    // order, so creating it here puts it at the far right of the toolbar.
    m_btnSearch = new Fl_Button(0, 0, ToolbarLayout::kIconButton, ToolbarLayout::kButtonHeight);
    m_btnSearch->box(FL_FLAT_BOX);
    m_btnSearch->tooltip("Search torrents");
    m_btnSearch->callback(onSearchToggle, this);
    // Prefer the supplied PNG, falling back to the built-in magnifier so a
    // missing asset never leaves the button blank.
    if (m_findIcon) {
        m_btnSearch->image(m_findIcon);
    } else if (Resources::getSearchIcon()) {
        m_btnSearch->image(Resources::getSearchIcon());
    }
    
    // Last child, mirroring the leading spacer, so the bar keeps the full window
    // width instead of shrinking to its content.
    m_trailSpacer = new Fl_Box(0, 0, 0, ToolbarLayout::kButtonHeight);
    m_trailSpacer->box(FL_NO_BOX);

    m_toolbarPack->end();
    m_toolbar->end();

    // kChildCount is derived from the table, but the pack is built out of
    // individual `new` calls, so nothing in the types ties the two together: add
    // a fourteenth widget here and every gap count in ToolbarLayout is one too
    // small, with no compile error and nothing visibly wrong except a bar that
    // is a few pixels out of centre. This is the one place that can see both
    // numbers, so it is where the disagreement is caught. Fl::error() alone
    // only prints, so this aborts rather than carrying on with a wrong bar.
    if (m_toolbarPack->children() != ToolbarLayout::kChildCount) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "toolbar pack holds %d children but ToolbarLayout::kChildCount "
                 "is %d: a widget was added or removed without updating the "
                 "table, and every gap count will now be wrong",
                 m_toolbarPack->children(), ToolbarLayout::kChildCount);
        Fl::error("%s", msg);
        std::abort();
    }

    // Start from the widest layout and let resize() narrow it down if needed
    layoutToolbar(w());
}

void MainWindow::createTorrentList() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int list_h = h() - y - STATUS_HEIGHT;
    
    // TorrentListWidget's constructor already restores the current group with
    // its own end(), so the widgets created after this still land in the window.
    m_torrentList = new TorrentListWidget(0, y, w(), list_h);
}

// The search view is a panel below the toolbar: the toolbar stays untouched
// and visible, only the area where the torrent list lives is replaced.
// Child coordinates are absolute (window) coordinates, as everywhere in FLTK.

void MainWindow::layoutToolbar(int available) {
    if (!m_toolbar || !m_toolbarPack) return;

    // The numbers come from ToolbarLayout; this function only moves widgets.
    ToolbarLayout::Level level = ToolbarLayout::forWidth(available);
    const ToolbarLayout::Spec& s = ToolbarLayout::spec(level);
    int slack = available - ToolbarLayout::requiredWidth(level);
    if (slack < 0) slack = 0;

    // Both parts have to match before we skip the work. Comparing the level
    // alone would leave the bar uncentred when the window is widened without
    // changing level, which is the common case on a wide screen.
    if (level == m_toolbarLevel && slack == m_toolbarSlack) return;
    m_toolbarLevel = level;
    m_toolbarSlack = slack;

    const int grow = ToolbarLayout::growPerButton(slack);

    auto setButton = [](Fl_Button* b, const char* label, int width) {
        b->label(label);
        b->resize(b->x(), b->y(), width, b->h());
    };
    // Visibility is ToolbarLayout's rule to answer, not this function's: the
    // pack drops a hidden child and the gap on either side of it, which is why
    // requiredWidth() counts the gap anyway. A zero-width spacer draws nothing,
    // so it is in the pack purely to hold those two gaps.
    auto setSpacer = [](Fl_Box* spacer, int width) {
        if (ToolbarLayout::spacerStaysVisible(width)) spacer->show();
        else spacer->hide();
        spacer->resize(spacer->x(), spacer->y(), width, spacer->h());
    };
    auto setWidth = [](Fl_Widget* w, int width) {
        w->resize(w->x(), w->y(), width, w->h());
    };

    // Half the slack the buttons did not take, on the left. Fl_Pack leaves the
    // rest as the margin on the right, so the block sits in the middle.
    const int margin = ToolbarLayout::margin(slack);
    setSpacer(m_leadSpacer, margin);
    setSpacer(m_trailSpacer, margin);

    // The labelled buttons widen to use some of the spare room, up to the cap.
    setButton(m_btnAdd, s.addLabel, s.addWidth + grow);
    setButton(m_btnCreate, s.createLabel, s.createWidth + grow);
    setButton(m_btnPrefs, s.prefsLabel, s.prefsWidth + grow);
    setSpacer(m_spacer1, s.spacerWidth);
    setSpacer(m_spacer2, s.spacerWidth);

    // Fixed-size widgets. The limit button is one of them: it carries an icon
    // only, so it never follows the level and never widens.
    setWidth(m_btnTogglePause, ToolbarLayout::kIconButton);
    setWidth(m_btnRemove, ToolbarLayout::kIconButton);
    setWidth(m_darkModeBtn, ToolbarLayout::kThemeButton);
    setWidth(m_btnRamMode, ToolbarLayout::kRamButton);
    setWidth(m_btnSearch, ToolbarLayout::kIconButton);
    setWidth(m_btnLimit, ToolbarLayout::kIconButton);
    setWidth(m_btnNetwork, ToolbarLayout::kIconButton);

    // The row of buttons keeps the pack's height policy on every pass, so a
    // resize cannot leave it stretched from the previous layout. The pack
    // stretches its children to its own height, so this is also what keeps the
    // buttons at kButtonHeight instead of the bar's.
    const int pad = ToolbarLayout::kBarPadding;
    m_toolbarPack->resize(m_toolbar->x(), m_toolbar->y() + pad,
                          available, ToolbarLayout::kButtonHeight);

    applyLimitStyle();
    m_toolbar->redraw();
}

// Kept in its own function because both the theme and the toggle need it. The
// button carries no text, so the limit state is shown by the bolt's colour
// alone: magenta while the limit is on, the toolbar's own background while it
// is off.

void MainWindow::createStatusBar() {
    int y = h() - STATUS_HEIGHT;
    
    m_statusGroup = new Fl_Group(0, y, w(), STATUS_HEIGHT);
    m_statusGroup->box(FL_DOWN_BOX);
    m_statusGroup->begin();
    
    // Status text (flexible width)
    m_statusBar = new Fl_Box(2, y + 2, w() - 35, STATUS_HEIGHT - 4);
    m_statusBar->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_statusBar->label("Ready");
    
    // Censor button (fixed width, at the right)
    m_btnCensor = new Fl_Button(w() - 30, y + 2, 26, STATUS_HEIGHT - 4);
    m_btnCensor->box(FL_FLAT_BOX);
    m_btnCensor->clear_visible_focus();
    m_btnCensor->callback(onToggleCensorship, this);
    m_btnCensor->tooltip("Hide/Show Sensitive Info");
    
    m_statusGroup->end();
    m_statusGroup->resizable(m_statusBar); // Text box takes extra space
    
    updateCensorButtonState();
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
            // Always show dialog for confirmation and file selection
            showAddTorrentDialog(path);
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
    
    // restoreWindowState() runs after the constructor already read these flags,
    // and this load() just replaced the in-memory map with what is on disk, so
    // they have to be read again or the saved value would be ignored.
    m_censored = settings.getIpCensored();
    m_showPublicIp = settings.getShowPublicIp();
    updateCensorButtonState();
    updateStatusBar();
    
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
    
    // Only m_torrentList is resizable, so FLTK grows/shrinks just that one.
    // Everything else has to be repositioned by hand, otherwise the toolbar
    // and the status bar keep their construction geometry and the status bar
    // ends up floating in the middle of the window (hiding the scrollbar).
    int listH = h - MENU_HEIGHT - TOOLBAR_HEIGHT - STATUS_HEIGHT;
    if (listH < 40) listH = 40;
    
    if (m_toolbar) {
        m_toolbar->resize(0, MENU_HEIGHT, w, TOOLBAR_HEIGHT);
        layoutToolbar(w);   // Narrow the toolbar down to what actually fits
    }
    if (m_torrentList) {
        m_torrentList->resize(0, MENU_HEIGHT + TOOLBAR_HEIGHT, w, listH);
    }
    layoutSearchView();
    if (m_statusGroup) {
        int sy = h - STATUS_HEIGHT;
        m_statusGroup->resize(0, sy, w, STATUS_HEIGHT);
        updateCensorButtonState(); // repositions the eye button and the status text
    }
}

void MainWindow::onAddTorrent(Fl_Widget*, void* data) {
    ((MainWindow*)data)->showAddTorrentDialog();
}

void MainWindow::onCreateTorrent(Fl_Widget*, void* data) {
    ((MainWindow*)data)->showCreateTorrentDialog();
}

void MainWindow::onTogglePause(Fl_Widget*, void* data) {
    ((MainWindow*)data)->toggleSelectedTorrents();
}

void MainWindow::onRemove(Fl_Widget*, void* data) {
    ((MainWindow*)data)->removeSelectedTorrents();
}

void MainWindow::onPreferences(Fl_Widget*, void* data) {
    ((MainWindow*)data)->showPreferencesDialog();
}

void MainWindow::onToggleTheme(Fl_Widget*, void* data) {
    ((MainWindow*)data)->toggleDarkMode();
}

void MainWindow::onToggleLimit(Fl_Widget*, void* data) {
    ((MainWindow*)data)->toggleNetworkLimit();
}

void MainWindow::onToggleCensorship(Fl_Widget*, void* data) {
    ((MainWindow*)data)->toggleCensorship();
}

void MainWindow::onSearchToggle(Fl_Widget*, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (win) win->switchView(!win->m_showSearchView);
}

void MainWindow::onSearchGo(Fl_Widget*, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (win) win->startSearch();
}

void MainWindow::onNetworkMonitor(Fl_Widget*, void* data) {
    MainWindow* win = static_cast<MainWindow*>(data);
    if (!win || !win->m_manager) return;
    // Modal, like the other windows, so the list behind it cannot be removed
    // from under the rows being read.
    NetworkMonitorDialog dialog(win->m_manager);
    dialog.show_modal();
}

void MainWindow::handleStatusRefresh(void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !s_alive) return;
    win->updateStatusBar();
}

void MainWindow::updateTimerCallback(void* data) {
    MainWindow* win = (MainWindow*)data;
    
    if (win && win->m_manager) {
        win->m_manager->update();
        win->updateToolbar(); // Keep toolbar updated as selection might change or items might disappear

        // Latency update every second (10 * 0.1s)
        win->m_latencyTicker++;
        if (win->m_latencyTicker >= 10) {
            win->m_latencyTicker = 0;
            // Run latency measurement in a separate thread to avoid UI freeze
            std::thread([win]() {
                int lat = SystemUtils::measureLatency();
                // Pass back to UI thread safely via Fl::awake
                Fl::awake(&MainWindow::handleStatusRefresh, win);
                win->m_latency = lat;
            }).detach();
        }

        // En Modo ECO, forzamos la liberación de memoria de forma agresiva cada 100ms
        if (SettingsManager::instance().getRamMode() == 0) {
            SystemUtils::releaseMemory();
        }
    }
    
    Fl::repeat_timeout(0.1, updateTimerCallback, data);
}

// Static callbacks
