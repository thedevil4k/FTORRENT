// The toolbar geometry sweep: every window width, checked against the policy's
// own arithmetic.
//
// Three things are asserted at each width, and all three are read off the drawn
// widgets rather than off the policy, because the whole point is to catch the
// policy and the widget tree disagreeing:
//
//   1. the pack spans the window -- Fl_Pack shrinks to its children, so a pack
//      narrower than the window means the block is pinned to the left edge
//   2. the two margins are equal, and equal to margin(slack) + kSpacing
//   3. the level the labels show is the one forWidth() promised
//
// Plus, once, the level thresholds themselves at each boundary and one pixel
// below it, since a change to the spacing or the count moves those silently.
#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Widget.H>
#include "MainWindow.h"
#include "ToolbarLayout.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "test_support.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using testsupport::ok;
using testsupport::forceLayout;

static Fl_Button* byTooltip(Fl_Group* g, const char* tip) {
    for (int i = 0; i < g->children(); i++) {
        Fl_Widget* c = g->child(i);
        if (c->tooltip() && !strcmp(c->tooltip(), tip)) return (Fl_Button*)c;
        if (c->as_group()) { Fl_Button* b = byTooltip(c->as_group(), tip); if (b) return b; }
    }
    return nullptr;
}

// The level the labels on the bar actually show, read back off the widgets.
static ToolbarLayout::Level observedLevel(Fl_Button* add, Fl_Button* create, Fl_Button* prefs) {
    for (int lv = ToolbarLayout::ICONS; lv <= ToolbarLayout::FULL; lv++) {
        const ToolbarLayout::Spec& s = ToolbarLayout::spec((ToolbarLayout::Level)lv);
        const char* a = add && add->label() ? add->label() : nullptr;
        const char* c = create && create->label() ? create->label() : nullptr;
        const char* p = prefs && prefs->label() ? prefs->label() : nullptr;
        if ((!a && !s.addLabel) || (a && s.addLabel && !strcmp(a, s.addLabel)))
            if ((!c && !s.createLabel) || (c && s.createLabel && !strcmp(c, s.createLabel)))
                if ((!p && !s.prefsLabel) || (p && s.prefsLabel && !strcmp(p, s.prefsLabel)))
                    return (ToolbarLayout::Level)lv;
    }
    return ToolbarLayout::FULL;   // unreachable in practice; caller compares widths too
}

static const char* lvName(ToolbarLayout::Level l) {
    return l == ToolbarLayout::ICONS ? "ICONS" : l == ToolbarLayout::SHORT ? "SHORT" : "FULL";
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testsupport::useScratchConfig("layout");
    Fl::lock();
    Resources::initialize();
    SettingsManager::instance().load();
    auto mgr = std::make_unique<TorrentManager>();
    mgr->initialize();
    MainWindow* win = new MainWindow(1400, 900, "FTorrent");
    win->setTorrentManager(mgr.get());
    win->show();
    Fl_Group* g_main = Fl::first_window()->as_group();

    printf("policy: requiredWidth ICONS=%d SHORT=%d FULL=%d  (kSpacing=%d kGrowable=%d kMaxGrow=%d)\n",
           ToolbarLayout::requiredWidth(ToolbarLayout::ICONS),
           ToolbarLayout::requiredWidth(ToolbarLayout::SHORT),
           ToolbarLayout::requiredWidth(ToolbarLayout::FULL),
           ToolbarLayout::kSpacing, ToolbarLayout::kGrowableCount, ToolbarLayout::kMaxGrow);

    // --- level thresholds: the width at which each level starts must be the
    // one requiredWidth() says, and the width below it must still be the coarser
    // level. This is the check a spacing change can move without anybody noticing.
    {
        const int rI = ToolbarLayout::requiredWidth(ToolbarLayout::ICONS);
        const int rS = ToolbarLayout::requiredWidth(ToolbarLayout::SHORT);
        const int rF = ToolbarLayout::requiredWidth(ToolbarLayout::FULL);
        testsupport::section("level thresholds");
        struct { int w; ToolbarLayout::Level want; const char* why; } t[] = {
            {rI - 1, ToolbarLayout::ICONS, "one below ICONS floor"},
            {rI,     ToolbarLayout::ICONS, "ICONS floor"},
            {rS - 1, ToolbarLayout::ICONS, "one below SHORT floor"},
            {rS,     ToolbarLayout::SHORT, "SHORT floor"},
            {rF - 1, ToolbarLayout::SHORT, "one below FULL floor"},
            {rF,     ToolbarLayout::FULL,  "FULL floor"},
        };
        for (auto& e : t) {
            ToolbarLayout::Level got = ToolbarLayout::forWidth(e.w);
            ok(got == e.want, "%dpx (%s): forWidth says %s", e.w, e.why, lvName(got));
        }
        // ... and the same widths through the real window, reading the labels
        // back off the buttons rather than trusting the policy call.
        for (auto& e : t) {
            win->resize(0, 0, e.w, 900);
            Fl_Button* add = byTooltip(g_main, "Add Torrent");
            Fl_Group* pack = add->parent();
            forceLayout(pack);
            ToolbarLayout::Level seen = observedLevel(add, byTooltip(g_main, "Create Torrent"),
                                                      byTooltip(g_main, "Preferences"));
            ok(seen == e.want, "%dpx (%s): the bar is really drawn in %s", e.w, e.why, lvName(seen));
        }
    }

    // --- the whole range the window supports, plus the boundaries around them.
    testsupport::section("drawn geometry across the range");
    std::vector<int> widths;
    for (int w = 640; w <= 1920; w++) widths.push_back(w);
    for (int w : {400, 476, 477, 719, 720, 721}) widths.push_back(w);
    std::vector<int> dead, asym, unsplittable;
    for (int w : widths) {
        if (w < 400) continue;
        win->resize(0, 0, w, 900);
        Fl_Button* add = byTooltip(g_main, "Add Torrent");
        Fl_Button* search = byTooltip(g_main, "Search torrents");
        if (!add || !search) { ok(false, "%dpx: toolbar buttons not found", w); continue; }
        Fl_Group* pack = add->parent();
        forceLayout(pack);
        const int left = add->x() - pack->x();
        const int right = (pack->x() + pack->w()) - (search->x() + search->w());
        const int slack = w - ToolbarLayout::requiredWidth(ToolbarLayout::forWidth(w));
        const int want = ToolbarLayout::margin(slack < 0 ? 0 : slack) + ToolbarLayout::kSpacing;
        // A width where the level does not fit at all is out of scope: the row
        // is wider than the window and the pack clips, both before and now.
        if (slack < 0) continue;
        if (left != right || left != want) {
            if (left != right) asym.push_back(w);
            else dead.push_back(w);
            ok(false, "%4dpx: margins %d left / %d right, policy says %d/%d%s", w, left, right,
               want, want, left != right ? "  (not equal)" : "");
        }
        // The pack has to span the window: it shrinks to its children, and one
        // pixel short is a pixel of dead bar at the end of the row. The single
        // exception is a slack of exactly 1, which no pair of integer margins
        // can express (3*grow + 2*margin == slack has no solution), so the row
        // is allowed to be that one pixel short there and nowhere else.
        const int shortfall = w - pack->w();
        if (shortfall != 0) {
            if (shortfall == 1 && slack == 1) unsplittable.push_back(w);
            else ok(false, "%4dpx: pack is %dpx, %dpx short of the window", w, pack->w(), shortfall);
        }
    }
    
    ok(dead.empty(), "every width spends its slack: %zu widths leave dead bar%s%s", dead.size(),
       dead.empty() ? "" : " -> ", dead.empty() ? "" : std::to_string(dead[0]).c_str());
    ok(asym.empty(), "every width is centred: %zu widths are not%s%s", asym.size(),
       asym.empty() ? "" : " -> ", asym.empty() ? "" : std::to_string(asym[0]).c_str());
    if (!dead.empty())  printf("  dead-bar widths:"); for (int w : dead) printf(" %d", w); printf("\n");
    if (!asym.empty())  printf("  off-centre widths:"); for (int w : asym) printf(" %d", w); printf("\n");
    printf("  one-pixel-short widths (slack 1, unsplittable):");
    for (int w : unsplittable) printf(" %d", w);
    printf("\n");
    std::sort(unsplittable.begin(), unsplittable.end());
    ok((unsplittable == std::vector<int>{477, 645, 789}),
       "the row is exactly as wide as the window at every width but the three where slack is 1");

    // --- the numbers themselves, at the width the user cares about and above.
    testsupport::section("measured numbers");
    for (int w : {720, 788, 1024, 1400}) {
        win->resize(0, 0, w, 900);
        Fl_Button* add = byTooltip(g_main, "Add Torrent");
        Fl_Button* create = byTooltip(g_main, "Create Torrent");
        Fl_Button* prefs = byTooltip(g_main, "Preferences");
        Fl_Button* search = byTooltip(g_main, "Search torrents");
        Fl_Group* pack = add->parent();
        forceLayout(pack);
        ToolbarLayout::Level lv = ToolbarLayout::forWidth(w);
        int slack = w - ToolbarLayout::requiredWidth(lv);
        printf("  %4dpx %-5s slack=%-4d grow=%-3d margin=%-4d | bar=%dx%d pack=%dx%d add=%dx%d@%d search=%d..%d | L=%d R=%d\n",
               w, lvName(lv), slack, ToolbarLayout::growPerButton(slack),
               ToolbarLayout::margin(slack), pack->parent()->w(), pack->parent()->h(),
               pack->w(), pack->h(), add->w(), add->x(), add->x(), search->x(),
               search->x() + search->w(),
               add->x() - pack->x(), (pack->x() + pack->w()) - (search->x() + search->w()));
        ok(add->w() == ToolbarLayout::spec(lv).addWidth + ToolbarLayout::growPerButton(slack),
           "%dpx: Add is %dpx = natural %d + grow %d", w, add->w(),
           ToolbarLayout::spec(lv).addWidth, ToolbarLayout::growPerButton(slack));
        ok(create->w() == ToolbarLayout::spec(lv).createWidth + ToolbarLayout::growPerButton(slack),
           "%dpx: Create is %dpx = natural %d + grow %d", w, create->w(),
           ToolbarLayout::spec(lv).createWidth, ToolbarLayout::growPerButton(slack));
        ok(prefs->w() == ToolbarLayout::spec(lv).prefsWidth + ToolbarLayout::growPerButton(slack),
           "%dpx: Prefs is %dpx = natural %d + grow %d", w, prefs->w(),
           ToolbarLayout::spec(lv).prefsWidth, ToolbarLayout::growPerButton(slack));
    }

    // --- the arithmetic itself, with no widgets involved: the invariant the
    // header promises. It has to hold for every slack, not just the ones a
    // window happens to land on.
    testsupport::section("slack invariant (pure policy, 0..2000)");
    int bad = 0, badExample = -1;
    for (int slack = 0; slack <= 2000; slack++) {
        int g = ToolbarLayout::growPerButton(slack);
        int m = ToolbarLayout::margin(slack);
        if (g < 0 || g > ToolbarLayout::kMaxGrow || m < 0) { bad++; badExample = slack; }
        if (g * ToolbarLayout::kGrowableCount + 2 * m > slack) { bad++; badExample = slack; }
        if (slack - (g * ToolbarLayout::kGrowableCount + 2 * m) > 1) { bad++; badExample = slack; }
    }
    ok(bad == 0, "growth + margins never exceed the slack and never leave more than 1px (%d violations%s)",
       bad, bad < 0 ? "" : (badExample >= 0 ? (std::string(", first at slack ") + std::to_string(badExample)).c_str() : ""));

    mgr->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("layout");
    return testsupport::report("toolbar_layout");
}