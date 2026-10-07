#include "MainWindow.h"
#include "CreateTorrentDialog.h"
#include "SettingsManager.h"
#include "RemoveConfirmDialog.h"
#include "Resources.h"
#include "SystemUtils.h"
#include "PathUtils.h"
#include "AssetLoader.h"
#include "ToolbarLayout.h"
#include "SearchManager.h"
#include "HttpClient.h"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>   // fl_choice, fl_input, fl_alert, fl_message all live here
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>
#include <fstream>
#include <thread>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// Helper to get country flag emoji
static std::string getFlagEmoji(std::string code) {
    if (code.length() != 2) return "";
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);
    std::string emoji = "";
    for (char c : code) {
        if (c >= 'A' && c <= 'Z') {
            emoji += (char)0xF0;
            emoji += (char)0x9F;
            emoji += (char)0x87;
            emoji += (char)(0xA6 + (c - 'A'));
        }
    }
    return emoji;
}

namespace {

// Bullet prefix marking a reachability-checked engine row. U+25CF, in the same
// geometric-shapes block as the ▲▼ sort indicators the tables already draw,
// so any font rendering those renders this too.
const char* kEngineCheckedBullet = "\xE2\x97\x8F ";

} // namespace

std::atomic<bool> MainWindow::s_alive{true};

MainWindow::MainWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
    , m_toolbar(nullptr)
    , m_leadSpacer(nullptr)
    , m_trailSpacer(nullptr)
    , m_statusGroup(nullptr)
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
    , m_manager(nullptr)
    , m_brightIcon(nullptr)
    , m_darkIcon(nullptr)
    , m_addIcon(nullptr)
    , m_createIcon(nullptr)
    , m_ecoIcon(nullptr)
    ,m_normalIcon(nullptr)
    ,m_turboIcon(nullptr)
    ,m_eyeOpenedIcon(nullptr)
    ,m_eyeClosedIcon(nullptr)
    ,m_findIcon(nullptr)
    ,m_limitIcon(nullptr)
    , m_engineStatusChecked(false)
    , m_probesStop(false)
    ,m_limitModerate(false)
    ,m_limitIdleColor(FL_BACKGROUND_COLOR)
    ,m_censored(false)    , m_showPublicIp(false)
    , m_darkMode(true)
    , m_toolbarLevel(ToolbarLayout::ICONS)
    , m_toolbarSlack(-1)
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
void MainWindow::createSearchView() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int viewH = h() - y - STATUS_HEIGHT;
    int barY = y + 4;
    int barH = SEARCH_BAR_HEIGHT - 10;
    
    m_searchView = new Fl_Group(0, y, w(), viewH);
    m_searchView->box(FL_FLAT_BOX);
    m_searchView->begin();
    
    // --- Bar: engine chooser, category, query, buttons ---
    m_choiceSearchEngine = new Fl_Choice(10, barY, 150, barH);
    m_choiceSearchEngine->box(FL_FLAT_BOX);
    m_choiceSearchEngine->tooltip("Search engine (green = reachable, red = unreachable, gray = not checked yet)");
    m_choiceSearchEngine->callback([](Fl_Widget* w, void* data) {
        MainWindow* win = (MainWindow*)data;
        if (win) win->updateCategoryChoice();
    }, this);
    
    m_choiceSearchCategory = new Fl_Choice(165, barY, 95, barH);
    m_choiceSearchCategory->box(FL_FLAT_BOX);
    m_choiceSearchCategory->tooltip("Category (depends on the engine)");
    m_choiceSearchCategory->hide();   // only engines with categories show it
    
    m_searchInput = new Fl_Input(170, barY, 300, barH);
    m_searchInput->tooltip("Type what to look for and press Enter");
    m_searchInput->when(FL_WHEN_ENTER_KEY);
    m_searchInput->callback([](Fl_Widget* w, void* data) {
        MainWindow* win = (MainWindow*)data;
        if (win) win->startSearch();
    }, this);
    
    m_btnSearchGo = new Fl_Button(480, barY, 80, barH, "Search");
    m_btnSearchGo->callback(onSearchGo, this);
    
    // Cancel, in red because it aborts the running search
    m_btnSearchCancel = new Fl_Button(795, barY, 100, barH, "Cancel");
    m_btnSearchCancel->box(FL_DOWN_BOX);
    m_btnSearchCancel->labelsize(13);
    m_btnSearchCancel->labelcolor(FL_RED);
    m_btnSearchCancel->selection_color(fl_rgb_color(200, 60, 60));
    m_btnSearchCancel->color(fl_rgb_color(64, 20, 20));
    m_btnSearchCancel->tooltip("Stop the search in progress");
    m_btnSearchCancel->callback(onSearchCancelClick, this);
    m_btnSearchCancel->hide();   // only while searching
    
    // --- Status line, under the bar and above the results ---
    int stY = y + SEARCH_BAR_HEIGHT;
    m_searchStatus = new Fl_Box(10, stY, w() - 20, SEARCH_STATUS_HEIGHT,
                                "Type something and press Enter");
    m_searchStatus->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_searchStatus->labelsize(11);
    m_searchStatus->labelcolor(fl_rgb_color(120, 120, 120));
    
    // --- Results fill the rest ---
    int resY = stY + SEARCH_STATUS_HEIGHT;
    int resH = h() - resY - STATUS_HEIGHT;
    m_searchResults = new SearchResultsWidget(0, resY, w(), resH);
    // (SearchResultsWidget's constructor already restores the current group)
    
    m_searchView->end();
    
    // Results go straight to the add dialog, same as a dropped .torrent file.
    // Only leaves the search view when something was actually added, so a
    // cancelled dialog does not throw the results away (notably on Windows,
    // where the dialog is the whole download affordance). The row's file list
    // travels along so a multi-file magnet offers one checkbox per file.
    m_searchResults->setOnDownloadCallback(
        [this](const SearchResult& r, const std::vector<TorrentFileEntry>& files) {
            if (showAddTorrentDialog("", r.magnet, files)) switchView(false);
        });
    
    m_searchView->hide();   // Torrent list is what you see first
    
    layoutSearchViewContents(w());   // replace the hardcoded bar widths
}

void MainWindow::switchView(bool showSearch) {
    m_showSearchView = showSearch;
    
    if (m_searchView) {
        if (showSearch) m_searchView->show();
        else m_searchView->hide();
    }
    if (m_torrentList) {
        if (showSearch) m_torrentList->hide();
        else m_torrentList->show();
    }
    if (m_btnSearch) {
        m_btnSearch->tooltip(showSearch ? "Back to my torrents" : "Search torrents");
    }

    // First visit to the search view: probe every engine once, in the
    // background, so the dropdown dots reflect reality. Once per session.
    if (showSearch && !m_engineStatusChecked) startEngineStatusCheck();

    // Re-run the layout: one view appeared and the other disappeared
    layoutSearchView();
    redraw();
}

// Positions whichever view is visible in the rectangle below the toolbar.
void MainWindow::layoutSearchView() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int listH = h() - y - STATUS_HEIGHT;
    if (listH < 40) listH = 40;
    int viewW = w();
    
    if (m_showSearchView) {
        if (m_searchView) m_searchView->resize(0, y, viewW, listH);
        layoutSearchViewContents(viewW);
    } else if (m_torrentList) {
        m_torrentList->resize(0, y, viewW, listH);
    }
}

void MainWindow::layoutSearchViewContents(int available) {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int barY = y + 4;
    int barH = SEARCH_BAR_HEIGHT - 10;
    
    // The query field takes whatever the fixed-width widgets leave, so the
    // bar adapts to the window instead of clipping the buttons.
    // Positions: 10 | engine | 5 | category? | 5 | input | 5 | Search
    const int wGo = 80, wCancel = 100;
    const int MARGIN_L = 10, MARGIN_R = 10, GAP = 5;
    int wEngine = (available < 820) ? 120 : 150;
    int wCat = (m_choiceSearchCategory && m_choiceSearchCategory->visible()) ? 95 : 0;
    int fixed = MARGIN_L + wEngine + GAP + wCat + (wCat > 0 ? GAP : 0)
              + GAP + wGo
              + GAP + wCancel + MARGIN_R;
    int wInput = available - fixed;
    if (wInput < 120) wInput = 120;
    
    m_choiceSearchEngine->resize(MARGIN_L, barY, wEngine, barH);
    int cx = MARGIN_L + wEngine + GAP;
    if (wCat > 0 && m_choiceSearchCategory) {
        m_choiceSearchCategory->resize(cx, barY, wCat, barH);
        cx += wCat + GAP;
    }
    m_searchInput->resize(cx, barY, wInput, barH);
    
    int bx = cx + wInput + GAP;
    m_btnSearchGo->resize(bx, barY, wGo, barH);
    bx += wGo + GAP;
    if (m_btnSearchCancel) m_btnSearchCancel->resize(bx, barY, wCancel, barH);
    
    // Status line spans the panel so long messages are not cut off
    int stY = y + SEARCH_BAR_HEIGHT;
    m_searchStatus->resize(MARGIN_L, stY, available - MARGIN_L - MARGIN_R, SEARCH_STATUS_HEIGHT);
    
    int resY = stY + SEARCH_STATUS_HEIGHT;
    int resH = h() - resY - STATUS_HEIGHT;
    if (resH < 40) resH = 40;
    
    if (m_searchResults) {
        m_searchResults->resize(0, resY, available, resH);
    }
}

// The toolbar has no layout manager that shrinks widgets for us: the pack lays
// every child out at the width it was constructed with, and if they add up to
// more than the window it grows past the window edge instead. So we ask
// ToolbarLayout which level of detail fits and apply it.
//
// The pack also stretches every child to its own height, which is why it is
// sized to kButtonHeight rather than to the bar height: the bar is the strip,
// the pack is the row of buttons inside it.
//
// The numbers live in ToolbarLayout; this function only moves widgets.
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
void MainWindow::applyLimitStyle() {
    if (!m_btnLimit) return;

    if (m_limitModerate) {
        m_btnLimit->color(fl_rgb_color(255, 0, 255));
    } else {
        m_btnLimit->color(m_limitIdleColor);
    }
    m_btnLimit->redraw();
}

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

void MainWindow::updateCensorButtonState() {
    if (!m_btnCensor) return;
    
    // The eye button only makes sense while the public IP is shown at all
    if (m_showPublicIp) {
        m_btnCensor->show();
        if (m_censored && m_eyeClosedIcon) {
            m_btnCensor->image(m_eyeClosedIcon);
        } else if (!m_censored && m_eyeOpenedIcon) {
            m_btnCensor->image(m_eyeOpenedIcon);
        }
    } else {
        m_btnCensor->hide();
        m_btnCensor->image(nullptr);
    }
    
    // Give the freed space back to the status text
    if (m_statusBar && m_statusGroup) {
        int rightMargin = m_showPublicIp ? 35 : 8;
        m_statusBar->resize(2, m_statusBar->y(), m_statusGroup->w() - rightMargin, m_statusBar->h());
        m_statusBar->redraw();
    }
    
    // Keep the eye button pinned to the right edge of the status bar
    if (m_btnCensor && m_statusGroup) {
        m_btnCensor->resize(m_statusGroup->w() - 30, m_statusGroup->y() + 2, 26, STATUS_HEIGHT - 4);
    }
    
    m_btnCensor->redraw();
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

// The censoring eye icons are the only images that follow the theme, so they
// are loaded again whenever it changes. Owning both halves here is what keeps
// the constructor and applyTheme() from drifting apart.
void MainWindow::reloadEyeIcons() {
    const bool dark = m_darkMode;

    delete m_eyeOpenedIcon;
    delete m_eyeClosedIcon;
    m_eyeOpenedIcon = AssetLoader::load(dark ? "eye_opened_bright.png" : "eye_opened_dark.png", ToolbarLayout::kIconSize);
    m_eyeClosedIcon = AssetLoader::load(dark ? "eye_closed_bright.png" : "eye_closed_dark.png", ToolbarLayout::kIconSize);
}

void MainWindow::applyTheme() {
    bool darkMode = m_darkMode;
    
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

    // The eye icons are the only images that follow the theme.
    reloadEyeIcons();

    if (m_btnCensor) {
        if (darkMode) {
            m_btnCensor->color(fl_rgb_color(20, 20, 20));
        } else {
            m_btnCensor->color(fl_rgb_color(225, 225, 225));
        }
    }
    updateCensorButtonState();
    
    // Aplicar a todos los widgets existentes
    for (int i = 0; i < children(); i++) {
        child(i)->redraw();
    }
    
    // Forzamos el rediseño de la ventana y sus hijos
    redraw();
    if (m_torrentList) {
        m_torrentList->refreshThemeColors(darkMode);
    }
    if (m_searchResults) {
        m_searchResults->applyTheme(darkMode);
    }
    
    // The limit button keeps its magenta while the limit is on, so its colour
    // is owned by applyLimitStyle(), which the toggle also goes through.
    if (m_btnLimit) applyLimitStyle();

    // The RAM button deliberately owns no colours: like every other toolbar
    // button it draws FL_BACKGROUND_COLOR with the default label color, so its
    // box is indistinguishable from the bar in both themes. Painting a custom
    // gray here is what left a visible box around it.
}

void MainWindow::toggleDarkMode() {
    // Session only, on purpose. This used to be written to settings.ini, which
    // is how a machine that had ever been left in light mode started light
    // from then on. Nothing is persisted now: the next launch is dark whatever
    // this button did here.
    m_darkMode = !m_darkMode;
    applyTheme();
}

void MainWindow::toggleNetworkLimit() {
    m_limitModerate = !m_limitModerate;
    
    if (m_btnLimit) {
        applyLimitStyle();
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

void MainWindow::toggleCensorship() {
    if (!m_showPublicIp) {
        return; // Nothing to censor while the public IP is disabled
    }
    
    m_censored = !m_censored;
    SettingsManager::instance().setIpCensored(m_censored);
    SettingsManager::instance().save();
    
    updateCensorButtonState();
    updateStatusBar();
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
    
    // Public IP (only when enabled in Preferences; the flag is hidden too while censored)
    if (m_showPublicIp) {
        std::string ip = m_manager->getPublicIp();
        if (!ip.empty()) {
            if (m_censored) {
                oss << "  |  IP: ***.***.***.***";
            } else {
                oss << "  |  IP: " << ip << " " << getFlagEmoji(m_manager->getCountryCode());
            }
        }
    }
    
    if (m_latency >= 0) {
        oss << "  |  LATENCY: " << m_latency << " ms";
    } else if (m_latency == -2) {
        oss << "  |  LATENCY: -- ms";
    }
    
    return oss.str();
}

bool MainWindow::showAddTorrentDialog(const std::string& prefilledPath, const std::string& prefilledMagnet,
                                       const std::vector<TorrentFileEntry>& prefilledFiles) {
    AddTorrentDialog* dlg = new AddTorrentDialog();

    if (!prefilledPath.empty()) {
        dlg->setTorrentPath(prefilledPath);
    }
    if (!prefilledMagnet.empty()) {
        dlg->setMagnetLink(prefilledMagnet);
        // A .torrent file fills the list itself; a magnet cannot, so the
        // search row's already-resolved list is what the checkboxes show.
        // Empty (never expanded / fetch failed) just means "download all".
        if (prefilledPath.empty() && !prefilledFiles.empty()) {
            dlg->setFileList(prefilledFiles);
        }
    }

    bool added = false;
    if (dlg->show_modal() && m_manager) {
        std::string path = dlg->getTorrentPath();
        std::string magnet = dlg->getMagnetLink();
        std::string savePath = dlg->getSavePath();
        auto filePriorities = dlg->getFilePriorities();

        bool success = false;
        if (!path.empty()) {
            success = m_manager->addTorrentFile(path, savePath, filePriorities);
        } else if (!magnet.empty()) {
            // Honour the checkboxes only when they describe this exact magnet:
            // the list was resolved for prefilledMagnet, so an edited field or
            // a length mismatch falls back to "download all" rather than
            // shifting libtorrent's file order.
            if (magnet != prefilledMagnet) filePriorities.clear();
            else if (filePriorities.size() != prefilledFiles.size()) filePriorities.clear();
            success = m_manager->addMagnetLink(magnet, savePath, filePriorities);
        }

        if (success) {
            updateUI();
            added = true;
        } else {
            fl_alert("Failed to add torrent");
        }
    }

    delete dlg;
    return added;
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
        auto& settings = SettingsManager::instance();
        m_censored = settings.getIpCensored();
        m_showPublicIp = settings.getShowPublicIp();
        updateCensorButtonState();
        updateStatusBar();
        updateUI();
    }
    
    delete dlg;
}

void MainWindow::showAboutDialog() {
    std::string message = "FTorrent v";
    message += VERSION;
    message += "\n\n"
               "A lightweight BitTorrent client built with FLTK\n\n"
               "Powered by libtorrent-rasterbar\n\n"
               "© 2026";
    fl_message("%s", message.c_str());
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
                    win->showAddTorrentDialog(path);
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

void MainWindow::onToggleCensorship(Fl_Widget* w, void* data) {
    ((MainWindow*)data)->toggleCensorship();
}

void MainWindow::onSearchToggle(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (win) win->switchView(!win->m_showSearchView);
}

void MainWindow::onSearchGo(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (win) win->startSearch();
}

void MainWindow::setSearchStatus(const std::string& text) {
    if (m_searchStatus) {
        m_searchStatus->copy_label(text.c_str());
        m_searchStatus->redraw();
    }
}

// Rebuilds the engine dropdown from SearchManager
void MainWindow::updateEngineChoice() {
    if (!m_choiceSearchEngine) return;

    // Drop the menu first: rows below borrow text owned here, so the menu
    // must never outlive a rebuild of these strings, whatever copy semantics
    // a given FLTK release uses for add()/label().
    m_choiceSearchEngine->clear();
    m_engineLabels.clear();
    int index = 0;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        // Gray bullet until probed; a cached green/red survives rebuilds.
        // Plain text rows with a per-row color: menu-item images proved
        // unrenderable on some FLTK builds, while text draws everywhere.
        m_engineLabels.push_back(engineLabel(d.name));
        m_choiceSearchEngine->add(m_engineLabels.back().c_str());
        Fl_Menu_Item* item =
            const_cast<Fl_Menu_Item*>(m_choiceSearchEngine->menu() + index);
        item->labelcolor(engineColor(engineStatusOf(d.name)));
        index++;
    }
    if (index > 0) m_choiceSearchEngine->value(0);
    m_choiceSearchEngine->redraw();
    updateCategoryChoice();
}

// Menu row of an engine, following the same enabled-only order as
// updateEngineChoice(). -1 when the engine is not listed.
int MainWindow::engineMenuIndex(const std::string& name) const {
    int index = 0;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        if (d.name == name) return index;
        index++;
    }
    return -1;
}

MainWindow::EngineStatus MainWindow::engineStatusOf(
    const std::string& name) const {
    auto it = m_engineStatus.find(name);
    if (it == m_engineStatus.end()) return EngineStatus::Unknown;
    return it->second;
}

Fl_Color MainWindow::engineColor(EngineStatus status) {
    // Gray reads on both themes; green/red carry the verdict.
    switch (status) {
        case EngineStatus::Up:   return fl_rgb_color(70, 190, 80);
        case EngineStatus::Down: return fl_rgb_color(220, 70, 70);
        default:                 return fl_rgb_color(150, 150, 150);
    }
}

std::string MainWindow::engineLabel(const std::string& name) {
    return std::string(kEngineCheckedBullet) + name;
}

// One joinable probe per enabled engine. Plain HttpClient GETs (the same door
// every search uses, no new libraries), short timeouts, and the window-dead
// check as the stop condition so closing the app aborts the probes instead of
// delivering into a freed window. Results hop to the UI thread via Fl::awake.
// Joinable rather than detached on purpose: a detached probe doing a blocking
// GET outlives main() and segfaults inside teardown, which is exactly what the
// toolbar_clicks suite caught. See shutdownEngineProbes().
void MainWindow::startEngineStatusCheck() {
    if (m_engineStatusChecked) return;   // once per session; also post-shutdown
    m_engineStatusChecked = true;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        SearchEngine::Definition def = d;   // the worker must not read the UI
        m_engineProbes.emplace_back([this, def] {
            if (!isAlive() || m_probesStop.load()) return;
            SearchEngine engine(def);
            HttpClient::Response resp = HttpClient::get(
                engine.buildUrl("test"), std::string("FTORRENT ") + VERSION,
                10,
                [this] {
                    return !MainWindow::isAlive() || m_probesStop.load();
                },
                nullptr, 6);
            if (!isAlive()) return;
            auto* delivery =
                new EngineStatusDelivery{this, def.name, resp.ok};
            Fl::awake(handleEngineStatus, delivery);
        });
    }
}

void MainWindow::shutdownEngineProbes() {
    // No new probes after this: a late switchView(true) must not resurrect
    // workers nobody will join. The flag aborts in-flight GETs at once so the
    // join below never waits out a full timeout.
    m_engineStatusChecked = true;
    m_probesStop.store(true);
    for (auto& t : m_engineProbes) {
        if (t.joinable()) t.join();
    }
    m_engineProbes.clear();
}

void MainWindow::handleEngineStatus(void* data) {
    std::unique_ptr<EngineStatusDelivery> delivery(
        static_cast<EngineStatusDelivery*>(data));
    if (!delivery || !isAlive() || !delivery->win) return;
    delivery->win->applyEngineStatus(delivery->name, delivery->up);
}

void MainWindow::applyEngineStatus(const std::string& name, bool up) {
    m_engineStatus[name] = up ? EngineStatus::Up : EngineStatus::Down;
    if (!m_choiceSearchEngine) return;
    int index = engineMenuIndex(name);
    if (index < 0) return;
    // Recolor in place: the text ("● Name", set at build time) is untouched,
    // so a status arrival can never blank a row.
    Fl_Menu_Item* item =
        const_cast<Fl_Menu_Item*>(m_choiceSearchEngine->menu() + index);
    item->labelcolor(engineColor(engineStatusOf(name)));
    m_choiceSearchEngine->redraw();
}

// Rebuilds the category dropdown from the selected engine. Engines with a
// single (or no) category hide the widget and always search "all".
void MainWindow::updateCategoryChoice() {
    if (!m_choiceSearchCategory) return;
    
    const SearchEngine::Definition* def = nullptr;
    if (m_choiceSearchEngine && m_choiceSearchEngine->value() >= 0) {
        int chosen = 0;
        for (const auto& d : SearchManager::instance().engines()) {
            if (!d.enabled) continue;
            if (chosen++ == m_choiceSearchEngine->value()) {
                def = &d;
                break;
            }
        }
    }
    
    m_choiceSearchCategory->clear();
    if (!def || def->categories.size() < 2) {
        m_choiceSearchCategory->hide();
    } else {
        int allAt = -1;
        int index = 0;
        for (const auto& kv : def->categories) {
            m_choiceSearchCategory->add(kv.first.c_str());
            if (kv.first == "All" || kv.first == "all") allAt = index;
            index++;
        }
        m_choiceSearchCategory->value(allAt >= 0 ? allAt : 0);
        m_choiceSearchCategory->show();
    }
    m_choiceSearchCategory->redraw();
    if (m_searchView && m_searchView->visible()) {
        layoutSearchViewContents(w());
        redraw();
    }
}

void MainWindow::loadSearchEngines() {
    // Only predefined engines: the catalogue lives in the code, so there is
    // nothing to load, merge or save. Stale plugin files from older versions
    // are ignored on purpose.
    SearchManager::instance().clear();
    for (const auto& d : SearchEngine::builtinEngines()) {
        SearchManager::instance().addEngine(d);
    }
    updateEngineChoice();
}

// Runs the query on a worker thread and hands the results back to the UI
// thread through Fl::awake, since widgets may only be touched from there.
void MainWindow::startSearch() {
    if (m_searching) {
        // A new search replaces the previous one instead of piling up: the old
        // thread sees the flag and stops at its next checkpoint, and the new one
        // takes over the flag below. Rows already shown are cleared once the
        // previous thread hands the state back, so the two never interleave.
        m_searchCancelled = true;
        if (m_btnSearchCancel) m_btnSearchCancel->deactivate();
    }
    
    if (!m_choiceSearchEngine || m_choiceSearchEngine->value() < 0) {
        setSearchStatus("No search engine available");
        return;
    }
    
    std::string query = m_searchInput ? m_searchInput->value() : "";
    if (query.empty()) {
        setSearchStatus("Type something and press Enter");
        return;
    }
    
    std::vector<SearchEngine::Definition> engines;
    int chosen = 0;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        if (chosen++ == m_choiceSearchEngine->value()) {
            engines.push_back(d);
            break;
        }
    }
    if (engines.empty()) {
        setSearchStatus("No search engine available");
        return;
    }
    
    // Category of the selected engine, or "all" when it declares none.
    std::string category = "all";
    if (m_choiceSearchCategory && m_choiceSearchCategory->visible() &&
        m_choiceSearchCategory->value() >= 0) {
        const char* picked = m_choiceSearchCategory->text(m_choiceSearchCategory->value());
        if (picked && picked[0] != '\0') category = picked;
    }
    
    // Each search gets a number, so a thread that was superseded can tell that
    // it no longer owns the state and hand it back untouched.
    const unsigned long generation = ++m_searchGeneration;
    m_searching = true;
    m_searchCancelled = false;
    setSearchStatus("Searching...");
    if (m_searchResults) m_searchResults->clear();
    {
        // The generation was just bumped above, so no batch from the search
        // being replaced can be parked after this point. Whatever it had
        // already parsed is dropped here: the table was just emptied, and
        // those rows belong to a search the user has moved on from.
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingBatch.clear();
    }
    if (m_btnSearchCancel) m_btnSearchCancel->show();
    if (m_btnSearchGo) m_btnSearchGo->deactivate();
    m_toolbar->redraw();
    
    std::thread([this, query, engines, generation, category]() {
        SearchEngine engine(engines[0]);
        
        auto isCurrent = [this, generation]() {
            return generation == m_searchGeneration;
        };
        
        // Rows are delivered as they are parsed rather than all at the end, so a
        // slow site fills the table progressively. Fl::awake only takes a plain
        // function, so the batch is parked under the mutex and picked up there.
        std::string error;
        engine.searchStreaming(query,
            [this, isCurrent, generation](const std::vector<SearchResult>& batch) {
                if (batch.empty() || !isCurrent()) return;
                {
                    std::lock_guard<std::mutex> lock(m_searchMutex);
                    // Checked again with the lock held: a new search may have
                    // taken over between the test above and this line, and its
                    // table was cleared. Parking a batch from the superseded
                    // search now would mix its rows into the new results.
                    if (generation != m_searchGeneration.load()) return;
                    m_pendingBatch.push_back(batch);
                }
                Fl::awake(&MainWindow::handleSearchBatch, this);
            },
            [this, isCurrent]() { return !isCurrent() || m_searchCancelled.load(); },
            error,
            category,
            [this, isCurrent](const std::string& stage) {
                if (!isCurrent() || stage.empty()) return;
                {
                    std::lock_guard<std::mutex> lock(m_searchMutex);
                    m_pendingStatus = stage;
                }
                Fl::awake(&MainWindow::handleSearchStatus, this);
            });
        
        // A newer search took over while this one was running: leave the state,
        // the table and the buttons to it.
        if (!isCurrent()) return;
        
        {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            m_pendingSearch.results.clear();
            m_pendingSearch.engine = engines[0].name;
            m_pendingSearch.error = error;
        }
        Fl::awake(&MainWindow::handleSearchDone, this);
    }).detach();
}

void MainWindow::appendSearchResults(const std::vector<SearchResult>& batch) {
    if (m_searchResults) m_searchResults->appendResults(batch);
}

void MainWindow::onSearchCancelClick(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !win->m_searching) return;
    
    // The flag alone is enough to stop an in-flight request: it is checked
    // several times per second by the transfer, so the worker unwinds on its
    // own and the UI is put back in order right away instead of after it.
    win->m_searchCancelled = true;
    win->m_searching = false;
    win->setSearchStatus("Search cancelled");
    
    if (win->m_btnSearchCancel) win->m_btnSearchCancel->hide();
    if (win->m_btnSearchGo) win->m_btnSearchGo->activate();
    if (win->m_searchResults) {
        int found = win->m_searchResults->rowCount();
        if (found > 0) {
            win->setSearchStatus("Cancelled with " + std::to_string(found) +
                                 " results so far");
        }
    }
    
    // Bumping the generation tells the worker thread that its results no longer
    // belong on screen, so nothing lands in the table after the cancel.
    ++win->m_searchGeneration;
}

// Paints the results parsed by the search thread, so rows appear while the
// search runs. Every batch waiting is painted, not just one: Fl::awake() wakes
// the UI once per batch but the payload is parked in m_pendingBatch, so taking
// a single slot silently threw away every batch but the last.
void MainWindow::handleSearchBatch(void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !s_alive) return;
    
    // The whole queue moves out at once, so each batch is appended exactly
    // once no matter how many times the UI thread is woken for it.
    std::deque<std::vector<SearchResult>> batches;
    {
        std::lock_guard<std::mutex> lock(win->m_searchMutex);
        batches.swap(win->m_pendingBatch);
    }
    if (batches.empty() || !win->m_searchResults) return;
    
    // Painted in the order the search produced them, so the table keeps the
    // engine's ordering until the user re-sorts it by clicking a header.
    for (const auto& batch : batches) {
        if (!batch.empty()) win->appendSearchResults(batch);
    }
    win->setSearchStatus("Found " +
        std::to_string(win->m_searchResults->rowCount()) + " results...");
}

// Restores the search controls once the worker thread is done.
void MainWindow::handleSearchDone(void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !s_alive) return;
    
    PendingSearch pending;
    {
        std::lock_guard<std::mutex> lock(win->m_searchMutex);
        pending = win->m_pendingSearch;
        win->m_pendingSearch.results.clear();
        win->m_pendingSearch.engine.clear();
        win->m_pendingSearch.error.clear();
    }
    
    win->m_searching = false;
    win->m_searchCancelled = false;
    if (win->m_btnSearchCancel) win->m_btnSearchCancel->hide();
    if (win->m_btnSearchGo) win->m_btnSearchGo->activate();
    win->onSearchResults(pending.results, pending.engine, pending.error);
}

// Paints a stage line posted by the search thread ("Contacting X..."), so a
// stall names its culprit instead of leaving a bare "Searching...".
void MainWindow::handleSearchStatus(void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !s_alive) return;
    
    std::string text;
    {
        std::lock_guard<std::mutex> lock(win->m_searchMutex);
        text.swap(win->m_pendingStatus);
    }
    if (!text.empty()) win->setSearchStatus(text);
}

// Repaints the status bar after the latency measurement finishes.
void MainWindow::handleStatusRefresh(void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !s_alive) return;
    win->updateStatusBar();
}

void MainWindow::onSearchResults(const std::vector<SearchResult>& results,
                                 const std::string& engineName,
                                 const std::string& error) {
    if (!results.empty() && m_searchResults) {
        m_searchResults->appendResults(results);
    }
    
    int count = m_searchResults ? m_searchResults->rowCount() : 0;
    if (count > 0) {
        setSearchStatus(std::to_string(count) + " results from " + engineName);
    } else {
        setSearchStatus(error.empty() ? "No results" : error);
    }
}

void MainWindow::updateRamModeButton() {
    if (!m_btnRamMode) return;
    // SettingsManager: 0 = ECO, 1 = Normal, 2 = TURBO. Anything else falls
    // back to Normal, which is also the fresh-install default.
    int mode = SettingsManager::instance().getRamMode();
    if (mode < 0 || mode > 2) mode = 1;
    Fl_Image* icon = nullptr;
    const char* label = " NORMAL";
    const char* tip = "RAM Usage Mode NORMAL (Balanced) — click for TURBO";
    if (mode == 0) {
        icon = m_ecoIcon;
        label = " ECO";
        tip = "RAM Usage Mode ECO (Zero Buffer) — click for NORMAL";
    } else if (mode == 2) {
        icon = m_turboIcon;
        label = " TURBO";
        tip = "RAM Usage Mode TURBO (Max Buffer) — click for ECO";
    } else {
        icon = m_normalIcon;
    }
    if (icon) m_btnRamMode->image(icon);
    m_btnRamMode->label(label);
    m_btnRamMode->tooltip(tip);
    m_btnRamMode->redraw();
}

void MainWindow::onRamModeCycle(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !win->m_manager) return;
    (void)w;
    int cur = SettingsManager::instance().getRamMode();
    int next = (cur == 1) ? 2 : (cur == 2) ? 0 : 1;   // NORMAL->TURBO->ECO
    SettingsManager::instance().setRamMode(next);
    SettingsManager::instance().save();
    win->m_manager->setRamMode(next);
    win->updateRamModeButton();
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
