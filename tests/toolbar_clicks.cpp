// Every toolbar button, clicked AT the minimum supported width.
//
// 720 is the narrowest width size_range() allows, and the width the centring
// work was done for, so it is where a button can end up outside the window or
// under its neighbour. The geometry sweep says where things ARE; this says they
// still respond there.
//
// Add/Create/Preferences are checked for position only: they open a MODAL
// dialog whose nested event loop never returns without a user, so a headless
// run cannot press them. Afterwards the bar is checked again at six widths,
// because a callback can hide a widget and a hidden child takes its pack gap
// with it -- which is exactly how the block used to end up off-centre.
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include "MainWindow.h"
#include "ToolbarLayout.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "test_support.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>

using testsupport::forceLayout;
using testsupport::ok;

static Fl_Widget* byTip(Fl_Widget* w, const char* t) {
    if (w->tooltip() && strcmp(w->tooltip(), t) == 0) return w;
    if (Fl_Group* g = w->as_group())
        for (int i = 0; i < g->children(); i++)
            if (Fl_Widget* h = byTip(g->child(i), t)) return h;
    return nullptr;
}

// The engine dropdown: the first Fl_Choice in the tree is the RAM mode
// selector, so match by tooltip prefix instead of by type alone.
static Fl_Choice* engineChoice(Fl_Widget* w) {
    if (Fl_Choice* c = dynamic_cast<Fl_Choice*>(w)) {
        const char* tip = c->tooltip();
        if (tip && strncmp(tip, "Search engine", 13) == 0) return c;
    }
    if (Fl_Group* g = w->as_group())
        for (int i = 0; i < g->children(); i++)
            if (Fl_Choice* hit = engineChoice(g->child(i))) return hit;
    return nullptr;
}

// Ink of one dropdown row drawn offscreen, same trick as icons.cpp: struct
// checks proved nothing (a row can hold a perfect multi-label and still draw
// blank when its draw function was never registered). Rendered on black AND
// on white, keeping the max: the click sequence above leaves the theme light,
// but nothing may assume that, and each rendering only shows the content
// whose color differs from its backdrop. Calibrated on the reference backend:
// a 12px dot alone is ~140px, dot + engine name ~700px, so the bar sits where
// only a row that really draws both can reach it.
static int menuRowInkOn(const Fl_Menu_Item* m, Fl_Choice* menu, Fl_Color bg) {
    const int W = 260, H = 24;
    Fl_Image_Surface surf(W, H);
    testsupport::SurfaceScope here(&surf);
    fl_color(bg);
    fl_rectf(0, 0, W, H);
    // Non-const call on purpose: Fl_Menu_Item::draw() is const on some FLTK
    // releases and not on others; this spelling compiles against both.
    const_cast<Fl_Menu_Item*>(m)->draw(2, 2, W - 4, H - 4, menu, 0);
    Fl_RGB_Image* img = surf.image();
    if (!img || !img->array || img->d() != 3) { delete img; return -1; }
    unsigned char br = 0, bgc = 0, bb = 0;
    Fl::get_color(bg, br, bgc, bb);
    int ink = 0;
    for (int y = 0; y < img->h(); y++)
        for (int x = 0; x < img->w(); x++) {
            const unsigned char* q = img->array + (y * img->w() + x) * 3;
            if (abs(q[0] - br) > 20 || abs(q[1] - bgc) > 20 ||
                abs(q[2] - bb) > 20)
                ink++;
        }
    delete img;
    return ink;
}

static int menuRowInk(const Fl_Menu_Item* m, Fl_Choice* menu) {
    int onBlack = menuRowInkOn(m, menu, FL_BLACK);
    int onWhite = menuRowInkOn(m, menu, FL_WHITE);
    if (onBlack < 0 || onWhite < 0) return -1;
    return onBlack > onWhite ? onBlack : onWhite;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testsupport::useScratchConfig("clicks");
    Fl::lock();
    Resources::initialize();
    SettingsManager::instance().load();
    auto mgr = std::make_unique<TorrentManager>();
    mgr->initialize();
    MainWindow* win = new MainWindow(1400, 900, "FTorrent");
    win->setTorrentManager(mgr.get());
    win->show();
    Fl_Widget* top = Fl::first_window();

    testsupport::section("clicks at the minimum width (720)");
    win->resize(0, 0, 720, 900);
    Fl::flush();
    forceLayout((Fl_Group*)top);

    const char* modal[] = {"Add Torrent", "Create Torrent", "Preferences"};
    for (auto o : modal) {
        Fl_Button* b = (Fl_Button*)byTip(top, o);
        char msg[160];
        if (!b) {
            snprintf(msg, sizeof msg, "%s button exists at 720px", o);
            ok(false, msg);
            continue;
        }
        snprintf(msg, sizeof msg,
                 "%s is fully on screen at 720px (%dx%d at x=%d..%d)",
                 o, b->w(), b->h(), b->x(), b->x() + b->w());
        ok(b->w() > 0 && b->h() > 0 && b->x() >= 0 && b->x() + b->w() <= 720, msg);
    }

    const char* order[] = {"Pause", "Remove Torrent", "Switch Theme",
                           "Limit network speed 50%", "Search torrents"};
    for (auto o : order) {
        Fl_Button* b = (Fl_Button*)byTip(top, o);
        char msg[160];
        if (!b) {
            snprintf(msg, sizeof msg, "%s button exists at 720px", o);
            ok(false, msg);
            continue;
        }
        snprintf(msg, sizeof msg, "%s clicked at 720px (%dx%d at x=%d..%d)",
                 o, b->w(), b->h(), b->x(), b->x() + b->w());
        ok(b->w() > 0 && b->h() > 0 && b->x() >= 0 && b->x() + b->w() <= 720, msg);
        b->do_callback();
        Fl::flush();
    }

    testsupport::section("bar still whole after clicking");
    for (int w : {720, 1024, 1400, 788, 644, 643}) {
        win->resize(0, 0, w, 900);
        Fl::flush();
        Fl_Button* a = (Fl_Button*)byTip(top, "Add Torrent");
        // The Search toggle renames itself once pressed, so look for either.
        Fl_Button* s = (Fl_Button*)byTip(top, "Search torrents");
        if (!s) s = (Fl_Button*)byTip(top, "Back to my torrents");
        if (!a || !s) {
            ok(false, "%dpx: the toolbar survived the click sequence", w);
            break;
        }
        Fl_Group* pack = a->parent();
        forceLayout(pack);
        const int left = a->x() - pack->x();
        const int right = (pack->x() + pack->w()) - (s->x() + s->w());
        char msg[160];
        snprintf(msg, sizeof msg, "%dpx after clicks: margins %d/%d, pack %dpx",
                 w, left, right, pack->w());
        ok(left == right, msg);
    }

    testsupport::section("search view opens, engine dots exist, view closes");
    // The click sequence above ends with the Search toggle in either state, so
    // drive it until the search view is open rather than assuming one click.
    for (int i = 0; i < 2 && !byTip(top, "Back to my torrents"); i++) {
        Fl_Button* t = (Fl_Button*)byTip(top, "Search torrents");
        if (!t) break;
        t->do_callback();
        Fl::flush();
    }
    ok(byTip(top, "Back to my torrents") != nullptr,
       "search view opened from the toggle (reachability probes launched)");
    // The dots start gray synchronously in updateEngineChoice(); green/red
    // arrive later from the background probes and depend on this run's
    // network, so only the gray baseline is asserted here.
    Fl_Choice* engines = engineChoice(top);
    ok(engines != nullptr, "engine dropdown exists");
    int nEngines = 0, nDotted = 0;
    if (engines) {
        // Fl_Menu_Item has no image getter: attaching one flips the labeltype
        // away from FL_NORMAL_LABEL (multi image+text label), which is what is
        // asserted here. True on FLTK 1.3 and 1.4 alike.
        for (const Fl_Menu_Item* m = engines->menu(); m && m->label(); ++m) {
            nEngines++;
            if (m->labeltype() != FL_NORMAL_LABEL) nDotted++;
        }
    }
    ok(nEngines > 0, "dropdown lists engines");
    ok(nDotted == nEngines && nEngines > 0,
       "every engine row carries a status dot");
    int nDrawn = 0;
    if (engines) {
        for (const Fl_Menu_Item* m = engines->menu(); m && m->label(); ++m) {
            int ink = menuRowInk(m, engines);
            char inkMsg[160];
            snprintf(inkMsg, sizeof inkMsg,
                     "engine row renders dot and name (%d ink px)", ink);
            ok(ink > 200, inkMsg);
            if (ink > 200) nDrawn++;
        }
    }
    ok(nDrawn == nEngines && nEngines > 0,
       "no engine row renders blank");
    // Let the probes run and deliver (Fl::awake is drained by Fl::wait); the
    // verdict itself is network-dependent and is not asserted, but surviving
    // the deliveries without hanging is.
    for (int i = 0; i < 20; i++) Fl::wait(0.1);
    Fl_Button* back = (Fl_Button*)byTip(top, "Back to my torrents");
    if (back) { back->do_callback(); Fl::flush(); }
    ok(byTip(top, "Search torrents") != nullptr,
       "toggle back restores the torrent list");

    // Join the probe workers before the teardown below starts destroying what
    // they use. A detached probe outliving main() was the exit segfault.
    win->shutdownEngineProbes();
    mgr->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("clicks");
    return testsupport::report("toolbar_clicks");
}