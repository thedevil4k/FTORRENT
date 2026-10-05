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
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>
#include "MainWindow.h"
#include "ToolbarLayout.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "test_support.h"

#include <cstdio>
#include <cstring>
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

    mgr->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("clicks");
    return testsupport::report("toolbar_clicks");
}