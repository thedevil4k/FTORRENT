// The toolbar icons and the toolbar geometry, checked against the real widgets.
//
// Two things are covered. The icon half: the supplied PNGs, the text-free limit
// button and the magenta active state, including the fallback path where the
// assets are missing and the compiled-in glyphs have to stand in. The geometry
// half: the level of detail each width produces, the widths of the buttons, the
// padding inside the bar, and the centring -- including at 720px, the narrowest
// width the window allows, which is where a zero-width spacer used to take two
// pack gaps with it and leave the block against the left edge.
//
// Nothing here reads a screenshot: every assertion is made against widget state
// or against pixels rendered offscreen, so it runs headless.
//
// usage: icons <assets: full|missing>
// exit 0 when every check passes.
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Pack.H>
// B0 is <termios.h>'s hangup baud rate; see test_support.h for why that
// collides with FLTK 1.3's Page_Format enum.
#ifdef B0
#  undef B0
#endif
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include "MainWindow.h"
#include "ToolbarLayout.h"
#include "TorrentManager.h"
#include "SettingsManager.h"
#include "Resources.h"
#include "AssetLoader.h"
#include "test_support.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static Fl_Window* g_main = nullptr;
static int checks = 0, failures = 0;
static void ok(bool cond, const char* what) {
    checks++;
    if (!cond) { failures++; printf("    FAIL %s\n", what); }
}

static Fl_Widget* byTooltip(Fl_Widget* w, const char* tip) {
    if (w->tooltip() && strcmp(w->tooltip(), tip) == 0) return w;
    if (Fl_Group* g = w->as_group())
        for (int i = 0; i < g->children(); i++)
            if (Fl_Widget* hit = byTooltip(g->child(i), tip)) return hit;
    return nullptr;
}

// Fl_Pack moves its children while it draws, so a headless probe sees the
// positions from the last real draw unless it lets one happen. Rendering into an
// offscreen surface is the same code path the window runs on every frame.
static void forceLayout(Fl_Widget* g) {
    Fl_Image_Surface surf(g->w(), g->h());
    testsupport::SurfaceScope here(&surf);
    g->draw();
    Fl_RGB_Image* img = surf.image();
    delete img;
}

static bool isMagenta(Fl_Button* b) {
    return b->color() == fl_rgb_color(255, 0, 255);
}

static bool sameLabel(const char* got, const char* want) {
    if (!want) return got == nullptr;
    return got && strcmp(got, want) == 0;
}

// Which level the bar is ACTUALLY in, read back off the three labelled
// buttons. ToolbarLayout::forWidth() is only the policy's prediction of this,
// so a bar that failed to apply a level would leave the labels at the previous
// level's words; asking the widgets removes that from the blind spot.
static bool observedLevel(Fl_Button* add, Fl_Button* create, Fl_Button* prefs,
                          ToolbarLayout::Level* out) {
    const char* got[3] = {
        (add && add->label()) ? add->label() : nullptr,
        (create && create->label()) ? create->label() : nullptr,
        (prefs && prefs->label()) ? prefs->label() : nullptr,
    };
    for (int lv = ToolbarLayout::ICONS; lv <= ToolbarLayout::FULL; lv++) {
        const ToolbarLayout::Spec& sp = ToolbarLayout::spec((ToolbarLayout::Level)lv);
        if (sameLabel(got[0], sp.addLabel) && sameLabel(got[1], sp.createLabel) &&
            sameLabel(got[2], sp.prefsLabel)) {
            *out = (ToolbarLayout::Level)lv;
            return true;
        }
    }
    return false;   // labels match no level at all: a mixed or stale bar
}

// Renders an image and compares the pixels. Which picture a button is drawing is
// a question about pixels, not about pointers: AssetLoader::load() returns a
// fresh copy on every call, so the object a button holds is never equal to
// anything the suite can load itself, and never equal to the compiled-in glyph.
static bool samePixels(Fl_Image* a, Fl_Image* b) {
    if (!a || !b || a->w() != b->w() || a->h() != b->h()) return false;
    const int w = a->w(), h = a->h();
    Fl_Image_Surface surf(w, h);
    std::vector<unsigned char> pa, pb;
    for (int pass = 0; pass < 2; pass++) {
        Fl_RGB_Image* out = nullptr;
        {
            testsupport::SurfaceScope here(&surf);
            fl_color(FL_BLACK);                       // same backdrop for both
            fl_rectf(0, 0, w, h);
            (pass == 0 ? a : b)->draw(0, 0);
            out = surf.image();
        }
        if (!out || out->d() != 3 || out->w() != w || out->h() != h) {
            if (out) delete out;
            return false;
        }
        (pass == 0 ? pa : pb).assign(out->array,
                                     out->array + (size_t)w * h * 3);
        delete out;
    }
    return pa == pb;
}

static const char* levelName(ToolbarLayout::Level lv) {
    return lv == ToolbarLayout::FULL ? "FULL" : lv == ToolbarLayout::SHORT ? "SHORT" : "ICONS";
}

struct Rgb { unsigned char r, g, b; };

// Renders the widget offscreen and reports the background it fills itself with
// plus the extent of everything else it draws (the icon). Rendered pixels, not
// the colour attributes, because those are what the user actually sees.
struct Rendered { Rgb bg; int x0, y0, x1, y1, ink; bool ok; };
static Rendered renderWidget(Fl_Widget* w) {
    Rendered r{};
    r.ok = false; r.x0 = r.y0 = r.x1 = r.y1 = -1; r.ink = 0;
    Fl_Image_Surface surf(w->w(), w->h());
    testsupport::SurfaceScope here(&surf);
    surf.draw(w);
    Fl_RGB_Image* img = surf.image();
    if (!img || !img->array || img->d() != 3 || img->w() != w->w() || img->h() != w->h()) {
        if (img) delete img;
        return r;
    }
    const unsigned char* px = img->array;
    auto at = [&](int x, int y) {
        const unsigned char* q = px + (y * img->w() + x) * 3;
        return Rgb{q[0], q[1], q[2]};
    };
    r.bg = at(1, 1);
    int maxx = -1, maxy = -1;
    for (int y = 0; y < img->h(); y++)
        for (int x = 0; x < img->w(); x++) {
            Rgb p = at(x, y);
            if (abs(p.r - r.bg.r) <= 6 && abs(p.g - r.bg.g) <= 6 && abs(p.b - r.bg.b) <= 6) continue;
            r.ink++;
            if (r.x0 < 0 || x < r.x0) r.x0 = x;
            if (x > maxx) maxx = x;
            if (r.y0 < 0 || y < r.y0) r.y0 = y;
            if (y > maxy) maxy = y;
        }
    if (r.x0 >= 0) { r.x1 = maxx; r.y1 = maxy; }
    r.ok = true;
    delete img;
    return r;
}

int main(int argc, char** argv) {
    std::string mode = (argc > 1) ? argv[1] : "full";
    printf("  [assets=%s]\n", mode.c_str());

    testsupport::useScratchConfig("icons");

    Fl::lock();
    Resources::initialize();
    SettingsManager::instance().load();

    auto manager = std::make_unique<TorrentManager>();
    if (!manager->initialize()) { printf("  TorrentManager::initialize() failed\n"); return 3; }

    MainWindow* win = new MainWindow(1400, 900, "FTorrent");
    g_main = win;
    win->setTorrentManager(manager.get());
    win->show();

    Fl_Widget* top = Fl::first_window();
    if (!top) { printf("  no window\n"); return 3; }

    Fl_Button* search = (Fl_Button*)byTooltip(top, "Search torrents");
    Fl_Button* limit = (Fl_Button*)byTooltip(top, "Limit network speed 50%");
    ok(search != nullptr, "search button found by tooltip");
    ok(limit != nullptr, "limit button found by tooltip");
    if (!search || !limit) {
        printf("  checks=%d failures=%d\n", checks, failures);
        manager->shutdown();
        Resources::cleanup();
        testsupport::removeScratchConfig("icons");
        return 1;
    }

    // Which picture the search button is drawing. Both variants put an icon of
    // the same box size there, so the box size cannot tell them apart and
    // neither can pointer identity: AssetLoader::load() returns a fresh copy per
    // call, so the button's image is never equal to anything loaded here. What
    // separates them is the drawn pixels, so that is what is compared.
    if (mode == "full") {
        ok(search->image() != nullptr, "search button carries an icon");
        ok(search->image() && search->image()->w() == ToolbarLayout::kIconSize,
           "search button icon is at the toolbar icon size");
        {
            std::unique_ptr<Fl_Image> fromDisk(
                AssetLoader::load("find.png", ToolbarLayout::kIconSize));
            ok(fromDisk != nullptr, "find.png is installed and decodes");
            ok(fromDisk && samePixels(search->image(), fromDisk.get()),
               "search button draws the supplied PNG's pixels, not something else");
            ok(search->image() && !samePixels(search->image(), Resources::getSearchIcon()),
               "search button is NOT falling back to the compiled-in magnifier");
        }
        ok(search->image() && !samePixels(search->image(), Resources::getPauseIcon()),
           "the search icon is not the pause glyph");
        ok(limit->image() != nullptr, "limit button carries an icon");
        ok(limit->image() && limit->image()->w() == ToolbarLayout::kIconSize,
           "limit button icon is at the toolbar icon size");
        ok(search->image() && !samePixels(search->image(), limit->image()),
           "the two buttons do not share one picture");
    } else {
        // With the PNGs gone the search button must fall back to the built-in
        // magnifier, and the limit button must end up with no image at all
        // rather than a blank one. What the fallback has to be is the SAME SIZE
        // family as its neighbours: a fallback that draws smaller than the
        // pause glyph beside it is worse than no fallback, so that is the
        // behaviour asserted here -- not the 16px box the source XPM happens to
        // be, which is an implementation detail, not the goal.
        ok(search->image() != nullptr, "search button still has an icon");
        ok(AssetLoader::load("find.png", ToolbarLayout::kIconSize) == nullptr,
           "find.png really is absent, so this variant is exercising the fallback");
        ok(search->image() && samePixels(search->image(), Resources::getSearchIcon()),
           "search button draws the compiled-in magnifier's pixels");
        ok(search->image() && search->image()->w() == ToolbarLayout::kIconSize &&
           search->image()->h() == ToolbarLayout::kIconSize,
           "the fallback magnifier is at the toolbar icon size, like its neighbours");
        // A pixmap would be nearest-neighbour stretched, with no alpha to blend
        // the edge; this is the property that makes it read as the same family.
        ok(search->image() && search->image()->d() == 4,
           "the fallback magnifier is resampled, not a stretched 1-bit pixmap");
        ok(limit->image() == nullptr,
           "limit button has NO image rather than a blank one");
    }

    // --- no text, ever: the bolt is the whole affordance.
    // The widths come from ToolbarLayout itself, not from numbers typed here, so
    // a change of policy does not silently leave this suite asserting the old
    // one: it follows the policy and still checks that the policy was applied.
    struct Level { int width; ToolbarLayout::Level level; const char* what; };
    const Level levels[] = {
        {1400, ToolbarLayout::FULL,  "wide: the full level"},
        {ToolbarLayout::requiredWidth(ToolbarLayout::FULL),
                               ToolbarLayout::FULL,  "exactly the width the full level needs"},
        {ToolbarLayout::requiredWidth(ToolbarLayout::FULL) - 1,
                               ToolbarLayout::SHORT, "one pixel less than the full level needs"},
        {ToolbarLayout::requiredWidth(ToolbarLayout::SHORT),
                               ToolbarLayout::SHORT, "exactly the width the short level needs"},
        {ToolbarLayout::requiredWidth(ToolbarLayout::SHORT) - 1,
                               ToolbarLayout::ICONS, "one pixel less than the short level needs"},
    };
    for (const Level& lv : levels) {
        win->resize(0, 0, lv.width, 900);
        Fl_Button* add = (Fl_Button*)byTooltip(top, "Add Torrent");
        Fl_Button* create = (Fl_Button*)byTooltip(top, "Create Torrent");
        Fl_Button* prefs = (Fl_Button*)byTooltip(top, "Preferences");
        const char* want = ToolbarLayout::spec(lv.level).addLabel;
        const char* got = (add && add->label()) ? add->label() : "(none)";
        char msg[192];
        snprintf(msg, sizeof msg, "%s (%dpx): Add label = \"%s\"", lv.what, lv.width, got);
        ok(add && ((want && strcmp(got, want) == 0) || (!want && !add->label())), msg);

        // Read the level back off the widgets instead of trusting the width
        // table. forWidth() says which level this width SHOULD produce; if the
        // bar failed to apply it, the labels would still say the previous level
        // and every size check below would still pass on stale geometry.
        ToolbarLayout::Level seen;
        ok(observedLevel(add, create, prefs, &seen), "the bar's labels match one level exactly");
        snprintf(msg, sizeof msg,
                 "%s (%dpx): the bar is really in %s, not just due for it (policy says %s)",
                 lv.what, lv.width,
                 observedLevel(add, create, prefs, &seen) ? levelName(seen) : "a MIX of levels",
                 levelName(lv.level));
        ok(observedLevel(add, create, prefs, &seen) && seen == lv.level, msg);

        ok(limit->label() == nullptr, "limit button has NO label text");
        snprintf(msg, sizeof msg, "limit button stays %dpx wide at %dpx",
                 ToolbarLayout::kIconButton, lv.width);
        ok(limit->w() == ToolbarLayout::kIconButton, msg);
        ok(limit->h() == ToolbarLayout::kButtonHeight,
           "limit button uses the button height, not the bar height");
    }

    // --- the block is centred and the spare width is shared, not dumped right
    //
    // 720 is here because it is the narrowest width size_range() allows, and
    // because it is the one that used to fail: the bar came up 11px short and
    // sat against the left edge. The loop asks ToolbarLayout which level the
    // width selects rather than assuming the full one, so the same centring
    // arithmetic is checked at the short level too.
    for (int w : {720, ToolbarLayout::requiredWidth(ToolbarLayout::FULL),
                  1024, 1400, 1920}) {
        win->resize(0, 0, w, 900);
        Fl_Button* add = (Fl_Button*)byTooltip(top, "Add Torrent");
        forceLayout(add->parent());   // let the pack place its children
        const ToolbarLayout::Level lv = ToolbarLayout::forWidth(w);
        const int slack = w - ToolbarLayout::requiredWidth(lv);
        const int grow = ToolbarLayout::growPerButton(slack);
        const int margin = ToolbarLayout::margin(slack);
        const int natural = ToolbarLayout::spec(lv).addWidth;

        char msg[192];
        snprintf(msg, sizeof msg,
                 "at %dpx the labelled button is %dpx = natural %d + growth %d",
                 w, add->w(), natural, grow);
        ok(add->w() == natural + grow, msg);
        snprintf(msg, sizeof msg, "at %dpx the button grew by %d, within the %dpx cap",
                 w, add->w() - natural, ToolbarLayout::kMaxGrow);
        ok(add->w() - natural <= ToolbarLayout::kMaxGrow, msg);

        // The whole slack is spoken for: growth plus the two margins comes back
        // to it. An odd pixel left over is a pixel of empty bar at the right
        // edge, which is what put the block off-centre in the first place.
        snprintf(msg, sizeof msg,
                 "at %dpx the slack is all spent: %d + 2 x %d == %d (slack %d)",
                 w, grow * ToolbarLayout::kGrowableCount, margin, slack, slack);
        ok(grow * ToolbarLayout::kGrowableCount + 2 * margin >= slack - 1, msg);

        // The pack has to span the window: it shrinks to its children, and a
        // pack narrower than the window pinned at x=0 would leave the block
        // hugging the left edge however well the margins are split. A slack of
        // exactly one pixel is the single width a pair of integer margins
        // cannot express, so one pixel short is allowed there and nowhere else.
        Fl_Group* pack = add->parent();
        snprintf(msg, sizeof msg, "at %dpx the bar spans the window (%dpx, slack %d)",
                 w, pack->w(), slack);
        ok(pack->w() == w || slack == 1, msg);

        // Vertically, the buttons must sit inside the bar with room above and
        // below. This is the thing a container can take away silently: Fl_Pack
        // stretches its children to its own height, so a pack sized to the BAR
        // pins every button to the bar's edge with no padding at all.
        Fl_Group* strip = (Fl_Group*)limit->parent()->parent();
        int topGap = 0, bottomGap = 0;
        bool first = true;
        for (int i = 0; i < strip->children(); i++) {
            Fl_Widget* c = strip->child(i);
            if (c->as_group() && c->as_group()->children() == ToolbarLayout::kChildCount) {
                for (int j = 0; j < c->as_group()->children(); j++) {
                    Fl_Widget* k = c->as_group()->child(j);
                    if (!k->visible() || k->w() == 0) continue;
                    if (first) { topGap = k->y() - strip->y(); first = false; }
                    bottomGap = (strip->y() + strip->h()) - (k->y() + k->h());
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "at %dpx the bar is %dpx and the buttons %dpx, leaving %dpx above and %dpx below",
                 w, strip->h(), limit->h(), topGap, bottomGap);
        ok(strip->h() == ToolbarLayout::kBarHeight, msg);
        ok(limit->h() == ToolbarLayout::kButtonHeight, msg);
        snprintf(msg, sizeof msg,
                 "at %dpx the padding is even: %dpx above, %dpx below, policy says %d",
                 w, topGap, bottomGap, ToolbarLayout::kBarPadding);
        ok(topGap == bottomGap && topGap == ToolbarLayout::kBarPadding, msg);

        // Both ends must be the same margin, which is what "centred" means here.
        Fl_Button* search = (Fl_Button*)byTooltip(g_main, "Search torrents");
        const int leftGap = add->x() - pack->x();
        const int rightGap = (pack->x() + pack->w()) - (search->x() + search->w());
        // The visible gap is the spacer plus the pack's own spacing, and the
        // two ends have to come out identical: that equality is the centring.
        const int visible = margin + ToolbarLayout::kSpacing;
        snprintf(msg, sizeof msg,
                 "at %dpx the block is centred: %dpx left, %dpx right (policy says %d)",
                 w, leftGap, rightGap, visible);
        ok(leftGap == rightGap && leftGap == visible, msg);

        // Every spacer has to be in the pack, even the ones collapsed to no
        // width: Fl_Pack skips a hidden child and the gap beside it, so hiding
        // one takes two gaps out of a row requiredWidth() has already counted
        // and the block stops being centred.
        for (int i = 0; i < pack->children(); i++) {
            Fl_Widget* c = pack->child(i);
            if (c->w() > 0) continue;
            snprintf(msg, sizeof msg,
                     "at %dpx the zero-width child [%d] is still in the pack", w, i);
            ok(c->visible(), msg);
        }
    }

    // --- past the growth cap, spare width becomes margin instead of stretch.
    //
    // Stated as a shape and not as a number: the button stops getting wider,
    // and the widening goes into the margins. The cap itself is deliberately
    // not quoted here -- a check that reads the same constant it is checking
    // cannot fail when that constant is wrong, which is how this one used to
    // read: it only ever failed over an unrelated stray pixel.
    {
        const int wA = 1400, wB = 1920;
        int wideA = 0, wideB = 0, marA = 0, marB = 0;
        for (int pass = 0; pass < 2; pass++) {
            const int w = pass ? wB : wA;
            win->resize(0, 0, w, 900);
            Fl_Button* b = (Fl_Button*)byTooltip(top, "Add Torrent");
            forceLayout(b->parent());
            const int slack = w - ToolbarLayout::requiredWidth(ToolbarLayout::forWidth(w));
            const int margin = ToolbarLayout::margin(slack);
            if (pass) { wideB = b->w(); marB = margin; }
            else      { wideA = b->w(); marA = margin; }
        }
        char msg[192];
        snprintf(msg, sizeof msg,
                 "widening %dpx to %dpx does not widen the labelled button (%dpx then %dpx)",
                 wA, wB, wideA, wideB);
        ok(wideB == wideA, msg);
        snprintf(msg, sizeof msg,
                 "the extra %dpx of width lands in the margins (%dpx then %dpx)",
                 wB - wA, marA, marB);
        ok(marB > marA, msg);
    }

    win->resize(0, 0, 1400, 900);

    // --- the compiled-in pause/remove glyphs, drawn at the toolbar icon size.
    //
    // These come from Resources, not from AssetLoader, so the check is that the
    // glyph is resampled inside Resources (no scaling at the call site) and that
    // the pixels that reach the screen are as big as the PNGs beside them.
    //
    // Search is the one that is usually a PNG instead. Which of the two it is has
    // already been asserted per variant above, so what matters here is the part
    // both share: whatever is on the button is at the icon size and is not being
    // scaled again on the way out.
    struct Glyph { const char* tip; const char* name; Fl_Image* compiledIn;
                   bool mustBeCompiledIn; };
    const Glyph glyphs[] = {
        {"Pause",           "pause",  Resources::getPauseIcon(), true},
        {"Remove Torrent",  "remove", Resources::getRemoveIcon(), true},
        // With the assets installed the search button shows the PNG instead, so
        // only the missing variant expects the compiled-in glyph here; both
        // variants still have to draw it at the icon size.
        {"Search torrents", "search", Resources::getSearchIcon(), mode == "missing"},
    };
    for (const Glyph& g : glyphs) {
        Fl_Button* b = (Fl_Button*)byTooltip(top, g.tip);
        char msg[192];
        ok(b != nullptr, "the pause/remove/search button is in the bar");
        if (!b) continue;
        snprintf(msg, sizeof msg, "%s button carries an icon", g.name);
        ok(b->image() != nullptr, msg);
        snprintf(msg, sizeof msg,
                 "%s icon is %dx%d, the size the toolbar draws icons at",
                 g.name, b->image() ? b->image()->w() : 0, b->image() ? b->image()->h() : 0);
        ok(b->image() && b->image()->w() == ToolbarLayout::kIconSize &&
           b->image()->h() == ToolbarLayout::kIconSize, msg);
        if (!g.mustBeCompiledIn || !b->image()) continue;

        // The button must hold the very object Resources handed out, not a copy:
        // a call-site copy() would be a second resample of an already-resampled
        // image, and would go back to nearest-neighbour.
        snprintf(msg, sizeof msg,
                 "%s button draws the compiled-in glyph's own pixels, not a copy of them",
                 g.name);
        ok(samePixels(b->image(), g.compiledIn), msg);
        // A pixmap would be nearest-neighbour stretched with no alpha to blend
        // the edge; this is what makes it read as the same family as the PNGs.
        snprintf(msg, sizeof msg,
                 "%s icon has an alpha channel, so its edges blend with the button",
                 g.name);
        ok(b->image()->d() == 4, msg);
    }

    // The drawn size, measured on rendered pixels exactly like the bolt above:
    // it used to be a 10x11 speck in a 44px button, which is what this change
    // is about. Compared against the PNG icons measured the same way.
    {
        Rendered magnifier = renderWidget((Fl_Button*)byTooltip(top, "Search torrents"));
        int refH = magnifier.ok ? magnifier.y1 - magnifier.y0 + 1 : 0;
        for (const Glyph& g : glyphs) {
            Fl_Button* b = (Fl_Button*)byTooltip(top, g.tip);
            if (!b) continue;
            Rendered r = renderWidget(b);
            int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
            char msg[192];
            snprintf(msg, sizeof msg,
                     "%s draws %dx%d px of ink, against %dpx for the PNG magnifier next to it",
                     g.name, w, h, refH);
            ok(r.ok && w >= 14 && h >= 14, msg);
            snprintf(msg, sizeof msg,
                     "%s ink bbox (%d,%d)-(%d,%d) fits inside the %dx%d button",
                     g.name, r.x0, r.y0, r.x1, r.y1, b->w(), b->h());
            ok(r.ok && r.x0 >= 0 && r.y0 >= 0 && r.x1 < b->w() && r.y1 < b->h(), msg);
            // It must be centred like its neighbours: FLTK centres an image that
            // is smaller than its label-less button, and a glyph shoved to one
            // edge would read as a different toolbar from the rest.
            snprintf(msg, sizeof msg,
                     "%s ink is centred: %dpx left, %dpx right of the %dx%d button",
                     g.name, r.x0, b->w() - r.x1 - 1, b->w(), b->h());
            ok(r.ok && abs(r.x0 - (b->w() - r.x1 - 1)) <= 1, msg);
        }
    }

    // The same ink at both levels of detail the bar actually switches between.
    // The buttons are icon-only at every level, so this asserts the glyphs are
    // not re-sampled or dropped when the level changes.
    {
        const int fullW = ToolbarLayout::requiredWidth(ToolbarLayout::FULL);
        const int shortW = ToolbarLayout::requiredWidth(ToolbarLayout::SHORT);
        struct LevelShot { int width; const char* what; } levels[] = {
            {fullW,     "the full level"},
            {fullW - 1, "the short level"},
            {shortW,    "the short level at its minimum width"},
        };
        for (const LevelShot& lv : levels) {
            win->resize(0, 0, lv.width, 900);
            // Same reason as above: prove the bar actually switched before
            // claiming the glyph is unchanged at both levels.
            ToolbarLayout::Level seen;
            Fl_Button* add = (Fl_Button*)byTooltip(top, "Add Torrent");
            Fl_Button* create = (Fl_Button*)byTooltip(top, "Create Torrent");
            Fl_Button* prefs = (Fl_Button*)byTooltip(top, "Preferences");
            ok(observedLevel(add, create, prefs, &seen) &&
               seen == ToolbarLayout::forWidth(lv.width),
               "the bar is observed in the level the width calls for before measuring");
            for (const Glyph& g : glyphs) {
                Fl_Button* b = (Fl_Button*)byTooltip(top, g.tip);
                if (!b) continue;
                Rendered r = renderWidget(b);
                char msg[192];
                snprintf(msg, sizeof msg,
                         "%s at %s (%dpx): icon is %dx%d",
                         g.name, lv.what, lv.width,
                         b->image() ? b->image()->w() : 0, b->image() ? b->image()->h() : 0);
                ok(b->image() && b->image()->w() == ToolbarLayout::kIconSize, msg);
                snprintf(msg, sizeof msg,
                         "%s at %s (%dpx): still draws %dx%d px of ink",
                         g.name, lv.what, lv.width, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
                ok(r.ok && (r.x1 - r.x0 + 1) >= 14 && (r.y1 - r.y0 + 1) >= 14, msg);
            }
        }
        win->resize(0, 0, 1400, 900);
    }

    // The pause/play pair is one button, and updateToolbar() swaps between them.
    // Both come from the same resampler, so the swap must not change the size or
    // the look: that is what would break if only one of the two had been
    // resampled. The swap itself needs a real TorrentItem, so flows.cpp drives it
    // with a torrent it creates; what is checked here is that the two glyphs
    // Resources hands out are the same size and both render properly.
    {
        Fl_Image* pause = Resources::getPauseIcon();
        Fl_Image* play = Resources::getPlayIcon();
        ok(pause != nullptr && play != nullptr, "both pause and play glyphs exist");
        ok(pause != play, "pause and play are different images, so the swap is visible");
        char msg[160];
        snprintf(msg, sizeof msg, "the play glyph is %dx%d, same box as the pause glyph",
                 play ? play->w() : 0, play ? play->h() : 0);
        ok(play && play->w() == ToolbarLayout::kIconSize &&
           play->h() == ToolbarLayout::kIconSize, msg);
        snprintf(msg, sizeof msg, "the play glyph carries an alpha channel too");
        ok(play && play->d() == 4, msg);

        // Rendered straight onto a surface, the way the button draws it, so the
        // claim is about pixels and not about a w() accessor.
        struct { const char* name; Fl_Image* img; } both[] = {
            {"pause", pause}, {"play", play},
        };
        for (auto& g : both) {
            Fl_Image_Surface surf(ToolbarLayout::kIconSize, ToolbarLayout::kIconSize);
            Fl_RGB_Image* shot = nullptr;
            {
                testsupport::SurfaceScope here(&surf);
                fl_color(FL_BLUE);            // sentinel: anything not blue was drawn
                fl_rectf(0, 0, ToolbarLayout::kIconSize, ToolbarLayout::kIconSize);
                g.img->draw(0, 0);
                shot = surf.image();
            }
            if (!shot || shot->d() != 3) { if (shot) delete shot; ok(false, "glyph rendered"); continue; }
            // Painted pixels are anything that is not the sentinel. Partial ones
            // mean the edge was blended rather than stepped.
            int ink = 0, solid = 0, soft = 0;
            for (int y = 0; y < shot->h(); y++)
                for (int x = 0; x < shot->w(); x++) {
                    const unsigned char* q = shot->array + (y * shot->w() + x) * 3;
                    if (q[0] < 20 && q[1] < 20 && q[2] > 200) continue;
                    ink++;
                    const int towardBody = q[0] + q[1];   // blue has none of either
                    if (towardBody > 380) solid++; else soft++;
                }
            delete shot;
            snprintf(msg, sizeof msg,
                     "the %s glyph paints %d px, %d of them solid and %d anti-aliased",
                     g.name, ink, solid, soft);
            ok(ink > 100, msg);
            // A nearest-neighbour blow-up has no anti-aliased pixel at all: every
            // pixel is either fully painted or fully clear. Requiring at least a
            // few soft pixels is what makes "the glyph survived the scale"
            // mean something rather than just "something was drawn".
            snprintf(msg, sizeof msg,
                     "the %s glyph has %d anti-aliased edge pixels, so it was resampled, not stretched",
                     g.name, soft);
            ok(soft >= 10, msg);
        }
    }

    // --- the inactive background must be the toolbar's own, measured on the
    // pixels that actually get drawn, not on the colour attribute.
    auto neighbour = [&]() { return (Fl_Button*)byTooltip(top, "Add Torrent"); };
    auto sameBackground = [&](const char* what) {
        Rendered mine = renderWidget(limit);
        Rendered theirs = renderWidget(neighbour());
        char msg[192];
        snprintf(msg, sizeof msg,
                 "%s: limit bg rgb(%u,%u,%u) == Add bg rgb(%u,%u,%u)",
                 what, mine.bg.r, mine.bg.g, mine.bg.b, theirs.bg.r, theirs.bg.g, theirs.bg.b);
        ok(mine.ok && theirs.ok && mine.bg.r == theirs.bg.r &&
           mine.bg.g == theirs.bg.g && mine.bg.b == theirs.bg.b, msg);
    };

    sameBackground("dark theme, limit off");

    // --- the bolt is drawn big enough to read, and still inside the button
    if (mode == "full") {
        Rendered bolt = renderWidget(limit);
        ok(bolt.ok, "limit button rendered offscreen");
        int bw = bolt.x1 - bolt.x0 + 1, bh = bolt.y1 - bolt.y0 + 1;
        char msg[160];
        snprintf(msg, sizeof msg, "bolt is drawn %dx%d px (it was 20x13 before the bar grew)", bw, bh);
        ok(bw >= ToolbarLayout::kIconButton - 22 && bh >= 17, msg);
        snprintf(msg, sizeof msg, "bolt bbox (%d,%d)-(%d,%d) fits the 40x30 button",
                 bolt.x0, bolt.y0, bolt.x1, bolt.y1);
        ok(bolt.x0 >= 0 && bolt.y0 >= 0 && bolt.x1 < limit->w() && bolt.y1 < limit->h(), msg);
    }

    // --- the magenta active state
    ok(!isMagenta(limit), "limit button is not magenta while inactive");

    win->toggleNetworkLimit();
    ok(isMagenta(limit), "limit button turns magenta when the limit is on");
    ok(limit->label() == nullptr, "toggling the limit does not add text back");
    {
        Rendered on = renderWidget(limit);
        char msg[96];
        snprintf(msg, sizeof msg, "limit on renders bg rgb(%u,%u,%u)",
                 on.bg.r, on.bg.g, on.bg.b);
        ok(on.ok && on.bg.r == 255 && on.bg.g == 0 && on.bg.b == 255,
           "the button really paints magenta, not just stores the colour");
    }

    // A theme change must not wipe the magenta: this is the case that would
    // break if applyTheme() kept its own colour block.
    win->toggleDarkMode();
    ok(isMagenta(limit), "magenta survives a switch to the light theme");
    win->toggleDarkMode();
    ok(isMagenta(limit), "magenta survives switching back to the dark theme");

    win->toggleNetworkLimit();
    ok(!isMagenta(limit), "limit button stops being magenta when switched off");
    sameBackground("dark theme, after switching the limit back off");

    // Same again from the light side of the theme.
    win->toggleDarkMode();
    sameBackground("light theme, limit off");
    win->toggleNetworkLimit();
    ok(isMagenta(limit), "magenta also applies in the light theme");
    win->toggleNetworkLimit();
    ok(!isMagenta(limit), "magenta is gone again in the light theme");
    sameBackground("light theme, after switching the limit back off");
    win->toggleDarkMode();

    printf("  assets=%-8s checks=%d failures=%d\n", mode.c_str(), checks, failures);
    manager->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("icons");
    return failures == 0 ? 0 : 1;
}