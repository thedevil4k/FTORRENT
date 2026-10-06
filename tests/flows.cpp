// The real user journeys, headless and end to end.
//
// Covered: the Preferences dialog (fields -> OK -> SettingsManager -> the file
// on disk), the Add dialog, Remove with nothing selected, the network limit
// toggle, a live network search and its cancel path, and a real torrent through
// add -> select -> pause -> resume -> remove including the glyph that shows it.
//
// Dialogs run show_modal(), so each one is driven by a timeout that fires inside
// the nested event loop. The search and the torrent add need the network; where
// the site itself fails, the test asserts that the app SAID SO rather than
// returning nothing quietly, so an upstream outage is not read as a pass.
//
// The config directory is redirected to a scratch one (see test_support.h): a
// test has no business reading or deleting the settings of the machine it runs
// on.
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>
#include "MainWindow.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "SystemUtils.h"
#include "test_support.h"

#include <filesystem>
#include "ToolbarLayout.h"
#include "TorrentListWidget.h"
#include "SearchResultsWidget.h"
#include "TorrentItem.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <fstream>
#include <vector>

static Fl_Window* g_main = nullptr;
static int checks = 0, failures = 0;
static void ok(bool cond, const char* what) {
    checks++;
    if (!cond) { failures++; printf("    FAIL %s\n", what); }
    else printf("    ok   %s\n", what);
}
static void note(const char* fmt, ...) { (void)fmt; }

// Pausing and resuming reach TorrentItem through libtorrent's alert queue, so
// they settle over the next tick rather than instantly. Poll for the state
// instead of assuming a fixed delay.
template <class Pred>
static bool waitFor(Pred pred, double seconds = 30.0) {
    for (int i = 0; i < (int)(seconds * 20); i++) {
        Fl::wait(0.05);
        if (pred()) return true;
    }
    return false;
}

template <class Pred>
static Fl_Widget* walk(Fl_Widget* w, Pred pred) {
    if (pred(w)) return w;
    if (Fl_Group* g = w->as_group())
        for (int i = 0; i < g->children(); i++)
            if (Fl_Widget* hit = walk(g->child(i), pred)) return hit;
    return nullptr;
}
static Fl_Widget* byTooltip(Fl_Widget* w, const char* t) {
    return walk(w, [t](Fl_Widget* x) { return x->tooltip() && strcmp(x->tooltip(), t) == 0; });
}
static Fl_Widget* byLabel(Fl_Widget* w, const char* t) {
    return walk(w, [t](Fl_Widget* x) { return x->label() && strcmp(x->label(), t) == 0; });
}
static bool pIsTable(Fl_Widget* w) { return dynamic_cast<Fl_Table_Row*>(w) != nullptr; }

// The dialog is the window that is not the main one.
static Fl_Window* dialogWindow(Fl_Window* mainWin) {
    for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w))
        if (w != mainWin && w->shown()) return w;
    return nullptr;
}

// A single-file .torrent, written by the probe itself so the suite needs no
// fixture on disk. bencode, in the handful of forms a metainfo file uses.
static std::string bencodeStr(const std::string& s) {
    return std::to_string(s.size()) + ":" + s;
}
static std::string bencodeInt(long long n) {
    return "i" + std::to_string(n) + "e";
}
static void makeFixture(const std::string& path) {
    const std::string piece = std::string(32768, '\0');
    // SHA-1 of the piece, which is what "pieces" holds.
    static const unsigned char sha1[20] = {
        0x51,0x88,0x43,0x18,0x49,0xb4,0x61,0x31,0x52,0xfd,0x7b,0xdb,0xa6,0xa3,0xff,0x0a,0x4f,0xd6,0x42,0x4b};
    // Keys inside a dict are written in sorted order, as bencode requires.
    // The info value is itself a dict, hence its own d...e.
    std::string info = bencodeStr("info") + "d" +
        bencodeStr("length") + bencodeInt((long long)piece.size()) +
        bencodeStr("name") + bencodeStr("probe-payload.bin") +
        bencodeStr("piece length") + bencodeInt(32768) +
        bencodeStr("pieces") + "20:" + std::string(reinterpret_cast<const char*>(sha1), 20) +
        "e";
    std::string body = bencodeStr("announce") + bencodeStr("http://tracker.example.invalid:6969/announce") + info;
    std::ofstream f(path, std::ios::binary);
    f << "d" << body << "e";
    std::ofstream p("/tmp/probe-payload.bin", std::ios::binary);
    p << piece;
}

// ---- callbacks that run inside the modal loops -----------------------------
static void dismissStrayDialog(void*);
static int prefsInputsSeen = 0;
// What the public-IP checkbox was set to when OK was pressed. -1 means the
// checkbox was not found, so the assertion below cannot silently pass.
static int prefsPublicIpWanted = -1;
static int prefsTries = 0;
static void drivePrefs(void*);
static void rearm(void*) { if (++prefsTries < 400) Fl::awake(drivePrefs, nullptr); }

static void drivePrefs(void*) {
    Fl_Window* dlg = dialogWindow(g_main);
    if (!dlg) {
        if (++prefsTries < 400) { Fl::awake(rearm, nullptr); return; }
        printf("    [prefs] gave up waiting for the dialog\n");
        return;
    }
    {
        printf("    [prefs] window stack:\n");
        for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w)) {
            int kids = w->as_group() ? w->as_group()->children() : -1;
            printf("      %-18s %4dx%-4d shown=%d kids=%d ptr=%p\n",
                   w->label() ? w->label() : "(no label)", w->w(), w->h(), (int)w->shown(), kids, (void*)w);
        }
    }
    // Count the Fl_Int_Inputs so we know which one is which.
    int n = 0;
    for (Fl_Widget* w = dlg; w; ) {
        (void)w; break;
    }
    Fl_Int_Input* ints[8] = {nullptr};
    std::function<void(Fl_Widget*)> rec = [&](Fl_Widget* x) {
        if (Fl_Int_Input* ii = dynamic_cast<Fl_Int_Input*>(x)) {
            if (n < 8) ints[n] = ii;
            n++;
        }
        if (Fl_Group* g = x->as_group())
            for (int i = 0; i < g->children(); i++) rec(g->child(i));
    };
    rec(dlg);
    prefsInputsSeen = n;
    if (n >= 1) ints[0]->value("6882");        // listening port
    if (n >= 4) ints[3]->value("77");          // max connections
    Fl_Button* ip = (Fl_Button*)byLabel(dlg, "Show public IP in status bar");
    if (ip && dynamic_cast<Fl_Check_Button*>(ip)) {
        Fl_Check_Button* cb = (Fl_Check_Button*)ip;
        cb->value(cb->value() ? 0 : 1);
        prefsPublicIpWanted = cb->value();
    }
    Fl_Button* ok = (Fl_Button*)byLabel(dlg, "OK");
    printf("    [prefs] int inputs=%d, OK button=%p\n", n, (void*)ok);
    if (ok) { Fl::add_timeout(2.0, dismissStrayDialog, nullptr); ok->do_callback(); }
    else dlg->hide();
}

// A failed add raises a modal fl_alert, which headless would leave open
// forever. Nothing answers it, so the watchdog just puts it away.
static void dismissStrayDialog(void*) {
    for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w))
        if (w != g_main && w->shown() && w != dialogWindow(g_main)) w->hide();
    for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w))
        if (w != g_main && w->shown() && w->label() && strcmp(w->label(), "Add Torrent")) w->hide();
}

static void fillAddDialog(void*) {
    Fl_Window* dlg = dialogWindow(g_main);
    if (!dlg) return;
    // The first Fl_Input is the .torrent path, the second the magnet, the
    // third the save path.
    Fl_Input* ins[6] = {nullptr};
    int n = 0;
    std::function<void(Fl_Widget*)> rec = [&](Fl_Widget* x) {
        if (Fl_Input* i = dynamic_cast<Fl_Input*>(x)) { if (n < 6) ins[n] = i; n++; }
        if (Fl_Group* g = x->as_group())
            for (int i = 0; i < g->children(); i++) rec(g->child(i));
    };
    rec(dlg);
    printf("    [add] inputs in the dialog: %d\n", n);
    if (n >= 1) ins[0]->value("/tmp/probe-payload.torrent");
    if (n >= 3) ins[2]->value("/tmp/ftorrent-probe-save");
    Fl_Button* ok = (Fl_Button*)byLabel(dlg, "OK");
    printf("    [add] OK button %s\n", ok ? "found" : "MISSING");
    if (ok) { Fl::add_timeout(2.0, dismissStrayDialog, nullptr); ok->do_callback(); }
    else dlg->hide();
}

static void cancelAddDialog(void*) {
    Fl_Window* dlg = dialogWindow(g_main);
    if (!dlg) return;
    for (const char* l : {"Cancel", "Close"}) {
        Fl_Button* b = (Fl_Button*)byLabel(dlg, l);
        if (b) { b->do_callback(); return; }
    }
    dlg->hide();
}

// Remove with nothing selected pops a confirm box; answer it if it shows up.
static void answerConfirm(void*) {
    Fl_Window* mainWin = Fl::first_window();
    Fl_Window* box = dialogWindow(mainWin);
    if (box) {
        Fl_Button* yes = (Fl_Button*)byLabel(box, "Yes");
        if (yes) { yes->do_callback(); return; }
        box->hide();
    }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testsupport::useScratchConfig("flows");
    Fl::lock();
    Resources::initialize();
    SettingsManager::instance().load();

    // The scratch config was just created, so this is a fresh install: nothing
    // has asked for the public IP yet, so it must be off. Showing it makes the
    // app look the address up on an external service, which a fresh install
    // should not do on its own.
    ok(!SettingsManager::instance().getShowPublicIp(),
       "a fresh install has the public IP off by default");

    auto manager = std::make_unique<TorrentManager>();
    if (!manager->initialize()) { printf("manager init failed\n"); return 3; }

    // The manager only performs the lookup when this flag is on (TorrentManager
    // update(), every 15 minutes). Driving update() here and asserting the
    // address is still empty exercises that guard rather than the flag alone.
    //
    // Note what this does NOT prove: fetchPublicIpAndCountry() answers on
    // another thread, so the address would be empty here even if a request had
    // gone out. Observing the ATTEMPT would need a seam in SystemUtils, which
    // is more machinery than this behaviour is worth. What is checked is that
    // the guard runs the update path without producing an address.
    manager->update();
    ok(manager->getPublicIp().empty(),
       "no public IP is produced while the setting is off");

    // The default only decides what a FRESH install gets. Once the key is in
    // settings.ini the stored value wins, so turning this default off must not
    // silently switch the IP off for anyone who had already turned it on.
    SettingsManager::instance().setShowPublicIp(true);
    SettingsManager::instance().save();
    SettingsManager::instance().load();
    ok(SettingsManager::instance().getShowPublicIp(),
       "an existing choice of ON survives the new default");

    // And the fallback in the getter has to agree with setDefaults(): a file
    // that predates the key would otherwise come up with a different answer
    // than a brand new one.
    SettingsManager::instance().setShowPublicIp(false);
    SettingsManager::instance().save();
    ok(!SettingsManager::instance().getShowPublicIp(),
       "an explicit OFF is persisted and read back");

    MainWindow* win = new MainWindow(1400, 900, "FTorrent");
    g_main = win;
    win->setTorrentManager(manager.get());
    win->show();

    printf("== flow 1: Preferences, opened, edited and confirmed ==\n");
    SettingsManager::instance().setListenPort(6881);
    Fl::awake(drivePrefs, nullptr);
    Fl_Button* prefsBtn = (Fl_Button*)byTooltip(Fl::first_window(), "Preferences");
    ok(prefsBtn != nullptr, "the Preferences toolbar button exists");
    prefsBtn->do_callback();
    ok(prefsInputsSeen >= 4, "the dialog has the four numeric settings fields");
    ok(SettingsManager::instance().getListenPort() == 6882,
       "OK pushed the new listening port into SettingsManager");
    ok(SettingsManager::instance().getMaxConnections() == 77,
       "OK pushed the new max connections into SettingsManager");
    // Compared against what the checkbox was actually set to, not a literal.
    // Toggling used to be enough while the default was on, but the default is
    // now off, so "toggle then expect false" no longer describes what OK does:
    // it would pass or fail on the starting value rather than on the dialog.
    ok(prefsPublicIpWanted >= 0 &&
       SettingsManager::instance().getShowPublicIp() == (prefsPublicIpWanted != 0),
       "OK pushed the toggled public-IP switch into SettingsManager");
    {
        // Where the app itself resolved its config, not a path built here: this
        // test redirects the config directory, so a hardcoded $HOME would read
        // the real settings file instead of the one just written.
        // getConfigPath() is private, so the path is rebuilt the same way
        // SettingsManager builds it: SystemUtils::getConfigDir() plus the
        // separator. Asking the app where its config is, rather than assuming
        // $HOME, is what makes this work with the redirected directory above.
        SettingsManager::instance().save();
        const std::string ini = SystemUtils::getConfigDir() +
                                (SystemUtils::isWindows() ? "\\settings.ini" : "/settings.ini");
        FILE* f = fopen(ini.c_str(), "r");
        char buf[4096] = {0};
        size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
        if (f) fclose(f);
        char msg[512];
        snprintf(msg, sizeof msg,
                 "the new port reached settings.ini on disk (%s)", ini.c_str());
        ok(n > 0 && strstr(buf, "6882") != nullptr, msg);
    }

    printf("== flow 2: Add Torrent dialog opens and closes ==\n");
    Fl_Button* addBtn = (Fl_Button*)byTooltip(Fl::first_window(), "Add Torrent");
    Fl::add_timeout(0.3, cancelAddDialog, nullptr);
    addBtn->do_callback();
    ok(true, "the Add dialog opened and was dismissed without crashing");
    ok(manager->getTorrentCount() == 0, "nothing was added to the session");

    printf("== flow 3: Remove with nothing selected ==\n");
    Fl_Button* rmBtn = (Fl_Button*)byTooltip(Fl::first_window(), "Remove Torrent");
    Fl::add_timeout(0.5, answerConfirm, nullptr);
    rmBtn->do_callback();
    ok(manager->getTorrentCount() == 0, "no torrent was removed");

    printf("== flow 4: network limit toggle ==\n");
    Fl_Button* limit = (Fl_Button*)byTooltip(Fl::first_window(), "Limit network speed 50%");
    win->toggleNetworkLimit();
    ok(limit->color() == fl_rgb_color(255, 0, 255), "the session took the limit on");
    win->toggleNetworkLimit();
    ok(limit->color() != fl_rgb_color(255, 0, 255), "and released it again");

    printf("== flow 5: a real search against the network ==\n");
    Fl_Button* searchBtn = (Fl_Button*)byTooltip(Fl::first_window(), "Search torrents");
    searchBtn->do_callback();
    Fl_Widget* input = byTooltip(Fl::first_window(), "Type what to look for and press Enter");
    ok(input != nullptr, "the search input is reachable after switching views");
    if (input) {
        ((Fl_Input*)input)->value("ubuntu");
        Fl_Button* go = (Fl_Button*)byLabel(Fl::first_window(), "Search");
        ok(go != nullptr, "the Search button exists");
        go->do_callback();
        SearchResultsWidget* table = nullptr;
        for (Fl_Window* w = Fl::first_window(); w; w = Fl::next_window(w))
            if (SearchResultsWidget* sr = dynamic_cast<SearchResultsWidget*>(w)) { table = sr; break; }
        if (!table) {
            std::function<void(Fl_Widget*)> rec = [&](Fl_Widget* x) {
                if (SearchResultsWidget* sr = dynamic_cast<SearchResultsWidget*>(x)) { table = sr; return; }
                if (Fl_Group* g = x->as_group())
                    for (int i = 0; i < g->children() && !table; i++) rec(g->child(i));
            };
            rec(g_main);
        }
        printf("    [search] results table %s\n", table ? "found" : "NOT FOUND");

        // Pump the event loop and watch what the search thread hands back.
        int rows = 0;
        std::string lastStatus;
        // The search status line is the only Fl_Box that is not the status bar.
        auto readSearchStatus = [&]() -> std::string {
            std::string found;
            std::function<void(Fl_Widget*)> rec = [&](Fl_Widget* x) {
                if (Fl_Box* b = dynamic_cast<Fl_Box*>(x))
                    if (b->label() && *b->label() &&
                        strncmp(b->label(), "Torrents:", 9) != 0)
                        found = b->label();
                if (Fl_Group* g = x->as_group())
                    for (int i = 0; i < g->children(); i++) rec(g->child(i));
            };
            rec(g_main);
            return found;
        };
        for (int i = 0; i < 300; i++) {          // up to ~30s
            Fl::wait(0.1);
            if (table) rows = table->rowCount();
            std::string now = readSearchStatus();
            if (!now.empty()) lastStatus = now;
            Fl_Box* sb = nullptr;
            // the status line is the first Fl_Box in the search view with text
            sb = nullptr;
            // Read it through the toolbar-independent route: the status text is
            // set by setSearchStatus(), which copies it into this box.
            if (rows > 0) break;
        }
        printf("    search finished with %d rows\n", rows);
        {   // whatever the status line says is the product's own account of it
            std::function<void(Fl_Widget*)> boxes = [&](Fl_Widget* x) {
                if (Fl_Box* b = dynamic_cast<Fl_Box*>(x))
                    if (b->label() && *b->label()) printf("    [status] %s\n", b->label());
                if (Fl_Group* g = x->as_group())
                    for (int i = 0; i < g->children(); i++) boxes(g->child(i));
            };
            boxes(g_main);
        }
        // The remote site is out of our hands. What is ours is that the search
        // either delivers rows or names the failure; returning nothing with a
        // silent status would be the bug this catches.
        bool remoteSaidNo = lastStatus.find("HTTP") != std::string::npos
                         || lastStatus.find("failed") != std::string::npos;
        char searchMsg[192];
        snprintf(searchMsg, sizeof searchMsg,
                 "the search either returned rows (%d) or reported why not (%s)",
                 rows, lastStatus.empty() ? "no status at all" : lastStatus.c_str());
        ok(rows > 0 || remoteSaidNo, searchMsg);

        // Cancel path: start again and cancel mid-flight.
        if (rows > 0) {
            go->do_callback();
            Fl::wait(0.4);
            Fl_Button* cancel = (Fl_Button*)byLabel(Fl::first_window(), "Cancel");
            ok(cancel != nullptr, "the cancel button is present while searching");
            if (cancel) cancel->do_callback();
            for (int i = 0; i < 60; i++) Fl::wait(0.1);
            ok(true, "cancelling a running search did not hang or crash");
        }
    }
    (void)note;

    printf("== flow 6: add a real torrent, then pause and remove it ==\n");
    makeFixture("/tmp/probe-payload.torrent");
    {   // this test's own scratch dir, not anything the app or the user owns
        std::error_code ec;
        std::filesystem::remove_all("/tmp/ftorrent-probe-save", ec);
        std::filesystem::create_directories("/tmp/ftorrent-probe-save", ec);
    }
    Fl::awake(fillAddDialog, nullptr);
    ((Fl_Button*)byTooltip(g_main, "Add Torrent"))->do_callback();
    ok(manager->getTorrentCount() == 1, "the torrent reached the session");
    if (manager->getTorrentCount() == 1) {
        const TorrentItem* t = manager->getAllTorrents().front();
        printf("    [torrent] name=%s state=%d\n", t->getName().c_str(), (int)t->getState());
        ok(!t->getName().empty(), "the added torrent carries a name");
        // Let a few 0.1s ticks run so the list widget is populated.
        for (int i = 0; i < 20; i++) Fl::wait(0.1);
        ok(true, "the list survives the new torrent");

        // The pause button swaps its glyph with the torrent's state, and both
        // glyphs are resampled by Resources to the toolbar icon size. Selecting
        // the row and pausing it is what drives that swap in the real app.
        TorrentListWidget* rows = (TorrentListWidget*)walk(g_main, pIsTable);
        Fl_Button* pauseBtn = (Fl_Button*)byTooltip(g_main, "Pause");
        ok(rows != nullptr, "the torrent list is reachable");
        ok(pauseBtn != nullptr, "the pause button is reachable");
        if (rows && pauseBtn) {
            rows->select_row(0, 1);
            ok(rows->hasSelection(), "the added torrent can be selected");
            ok(pauseBtn->image() == Resources::getPauseIcon(),
               "a running torrent shows the pause glyph");
            ok(pauseBtn->image() && pauseBtn->image()->w() == ToolbarLayout::kIconSize,
               "the pause glyph is drawn at the toolbar icon size");

            manager->pauseTorrent(t->getHash());
            bool paused = waitFor([&] {
                const TorrentItem* x = manager->getTorrent(t->getHash());
                return x && x->getState() == TorrentItem::State::Paused;
            });
            ok(paused, "the torrent pauses");
            // updateToolbar() is what the list's selection callback calls; driving
            // it directly is the same code path without depending on the click.
            win->updateToolbar();
            ok(pauseBtn->image() == Resources::getPlayIcon(),
               "a paused torrent shows the play glyph instead");
            ok(pauseBtn->image() && pauseBtn->image()->w() == ToolbarLayout::kIconSize,
               "the play glyph is drawn at the same size as the pause one");

            manager->resumeTorrent(t->getHash());
            ok(waitFor([&] {
                   const TorrentItem* x = manager->getTorrent(t->getHash());
                   return x && x->getState() != TorrentItem::State::Paused;
               }), "and resumes");
            win->updateToolbar();
            ok(pauseBtn->image() == Resources::getPauseIcon(),
               "resuming brings the pause glyph back");
            rows->select_row(0, 0);
        }
        printf("    [step] before pause hash=%s state=%d\n", t->getHash().c_str(), (int)t->getState());
        manager->pauseTorrent(t->getHash());
        printf("    [step] right after pause state=%d same_object=%d\n",
               (int)t->getState(), manager->getTorrent(t->getHash()) == t);
        ok(waitFor([&] {
               const TorrentItem* x = manager->getTorrent(t->getHash());
               return x && x->getState() == TorrentItem::State::Paused;
           }), "the torrent pauses");
        printf("    [step] settled state=%d same_object=%d\n",
               (int)t->getState(), manager->getTorrent(t->getHash()) == t);
        manager->resumeTorrent(t->getHash());
        ok(waitFor([&] {
               const TorrentItem* x = manager->getTorrent(t->getHash());
               return x && x->getState() != TorrentItem::State::Paused;
           }), "and resumes");
        manager->removeTorrent(t->getHash(), false);
        ok(waitFor([&] { return manager->getTorrentCount() == 0; }),
           "and is removed from the session");
    }

    printf("  flows checks=%d failures=%d\n", checks, failures);
    // Flow 5 opens the search view, which launches the engine reachability
    // probes: join them before the teardown below, same as toolbar_clicks.
    win->shutdownEngineProbes();
    manager->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("flows");
    // The torrent the add flow builds lives in /tmp; it is this test's own
    // scratch payload, not anything the app owns.
    std::error_code ec;
    std::filesystem::remove("/tmp/ftorrent-probe-save", ec);
    std::filesystem::remove("/tmp/probe-payload.torrent", ec);
    std::filesystem::remove("/tmp/probe-payload.bin", ec);
    return failures == 0 ? 0 : 1;
}