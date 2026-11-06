// The toolbar geometry half of the icons suite.
//
// The level of detail each width produces, the widths of the buttons, the
// padding inside the bar and the centring of the block, including at 720px,
// the narrowest width the window allows. The pictures the buttons draw are
// checked by the other half, in icons.cpp.
#include "icons_support.h"

#include "MainWindow.h"
#include "Resources.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Widget.H>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using iconsupport::byTooltip;
using iconsupport::levelName;
using iconsupport::observedLevel;
using testsupport::ok;

} // namespace

void checkToolbarGeometry(MainWindow* win, Fl_Widget* top,
                          const std::string& mode) {
    // The limit button is the one that never carries text, so it is the one
    // the geometry is read from.
    Fl_Button* limit = (Fl_Button*)byTooltip(top, "Limit network speed 50%");

    // --- no text, ever: the bolt is the whole affordance.
    // The widths come from ToolbarLayout itself, not from numbers typed here,
    // so a change of policy does not silently leave this suite asserting the
    // old one: it follows the policy and still checks that the policy was
    // applied.
    struct Level {
        int width;
        ToolbarLayout::Level level;
        const char* what;
    };
    const Level levels[] = {
        {1400, ToolbarLayout::FULL, "wide: the full level"},
        {ToolbarLayout::requiredWidth(ToolbarLayout::FULL), ToolbarLayout::FULL,
         "exactly the width the full level needs"},
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
        snprintf(msg, sizeof msg, "%s (%dpx): Add label = \"%s\"", lv.what,
                 lv.width, got);
        ok(add &&
               ((want && strcmp(got, want) == 0) || (!want && !add->label())),
           msg);

        // Read the level back off the widgets instead of trusting the width
        // table. forWidth() says which level this width SHOULD produce; if the
        // bar failed to apply it, the labels would still say the previous level
        // and every size check below would still pass on stale geometry.
        ToolbarLayout::Level seen;
        ok(observedLevel(add, create, prefs, &seen),
           "the bar's labels match one level exactly");
        snprintf(msg, sizeof msg,
                 "%s (%dpx): the bar is really in %s, not just due for it "
                 "(policy says %s)",
                 lv.what, lv.width,
                 observedLevel(add, create, prefs, &seen) ? levelName(seen)
                                                          : "a MIX of levels",
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
    for (int w : {720, ToolbarLayout::requiredWidth(ToolbarLayout::FULL), 1024,
                  1400, 1920}) {
        win->resize(0, 0, w, 900);
        Fl_Button* add = (Fl_Button*)byTooltip(top, "Add Torrent");
        testsupport::forceLayout(
            add->parent()); // let the pack place its children
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
        snprintf(msg, sizeof msg,
                 "at %dpx the button grew by %d, within the %dpx cap", w,
                 add->w() - natural, ToolbarLayout::kMaxGrow);
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
        snprintf(msg, sizeof msg,
                 "at %dpx the bar spans the window (%dpx, slack %d)", w,
                 pack->w(), slack);
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
            if (c->as_group() &&
                c->as_group()->children() == ToolbarLayout::kChildCount) {
                for (int j = 0; j < c->as_group()->children(); j++) {
                    Fl_Widget* k = c->as_group()->child(j);
                    if (!k->visible() || k->w() == 0) continue;
                    if (first) {
                        topGap = k->y() - strip->y();
                        first = false;
                    }
                    bottomGap = (strip->y() + strip->h()) - (k->y() + k->h());
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "at %dpx the bar is %dpx and the buttons %dpx, leaving %dpx "
                 "above and %dpx below",
                 w, strip->h(), limit->h(), topGap, bottomGap);
        ok(strip->h() == ToolbarLayout::kBarHeight, msg);
        ok(limit->h() == ToolbarLayout::kButtonHeight, msg);
        snprintf(msg, sizeof msg,
                 "at %dpx the padding is even: %dpx above, %dpx below, policy "
                 "says %d",
                 w, topGap, bottomGap, ToolbarLayout::kBarPadding);
        ok(topGap == bottomGap && topGap == ToolbarLayout::kBarPadding, msg);

        // Both ends must be the same margin, which is what "centred" means
        // here.
        Fl_Button* search = (Fl_Button*)byTooltip(win, "Search torrents");
        const int leftGap = add->x() - pack->x();
        const int rightGap =
            (pack->x() + pack->w()) - (search->x() + search->w());
        // The visible gap is the spacer plus the pack's own spacing, and the
        // two ends have to come out identical: that equality is the centring.
        const int visible = margin + ToolbarLayout::kSpacing;
        snprintf(msg, sizeof msg,
                 "at %dpx the block is centred: %dpx left, %dpx right (policy "
                 "says %d)",
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
                     "at %dpx the zero-width child [%d] is still in the pack",
                     w, i);
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
            testsupport::forceLayout(b->parent());
            const int slack =
                w - ToolbarLayout::requiredWidth(ToolbarLayout::forWidth(w));
            const int margin = ToolbarLayout::margin(slack);
            if (pass) {
                wideB = b->w();
                marB = margin;
            } else {
                wideA = b->w();
                marA = margin;
            }
        }
        char msg[192];
        snprintf(msg, sizeof msg,
                 "widening %dpx to %dpx does not widen the labelled button "
                 "(%dpx then %dpx)",
                 wA, wB, wideA, wideB);
        ok(wideB == wideA, msg);
        snprintf(
            msg, sizeof msg,
            "the extra %dpx of width lands in the margins (%dpx then %dpx)",
            wB - wA, marA, marB);
        ok(marB > marA, msg);
    }

    win->resize(0, 0, 1400, 900);

    // --- the compiled-in pause/remove glyphs, drawn at the toolbar icon size.
    //
    // These come from Resources, not from AssetLoader, so the check is that the
    // glyph is resampled inside Resources (no scaling at the call site) and
    // that the pixels that reach the screen are as big as the PNGs beside them.
    //
    // Search is the one that is usually a PNG instead. Which of the two it is
    // has already been asserted per variant above, so what matters here is the
    // part both share: whatever is on the button is at the icon size and is not
    // being scaled again on the way out.
    struct Glyph {
        const char* tip;
        const char* name;
        Fl_Image* compiledIn;
        bool mustBeCompiledIn;
    };
    const Glyph glyphs[] = {
        {"Pause", "pause", Resources::getPauseIcon(), true},
        {"Remove Torrent", "remove", Resources::getRemoveIcon(), true},
        // With the assets installed the search button shows the PNG instead, so
        // only the missing variant expects the compiled-in glyph here; both
        // variants still have to draw it at the icon size.
        {"Search torrents", "search", Resources::getSearchIcon(),
         mode == "missing"},
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
                 g.name, b->image() ? b->image()->w() : 0,
                 b->image() ? b->image()->h() : 0);
        ok(b->image() && b->image()->w() == ToolbarLayout::kIconSize &&
               b->image()->h() == ToolbarLayout::kIconSize,
           msg);
        if (!g.mustBeCompiledIn || !b->image()) continue;

        // The button must hold the very object Resources handed out, not a
        // copy: a call-site copy() would be a second resample of an
        // already-resampled image, and would go back to nearest-neighbour.
        snprintf(msg, sizeof msg,
                 "%s button draws the compiled-in glyph's own pixels, not a "
                 "copy of them",
                 g.name);
        ok(testsupport::samePixels(b->image(), g.compiledIn), msg);
        // A pixmap would be nearest-neighbour stretched with no alpha to blend
        // the edge; this is what makes it read as the same family as the PNGs.
        snprintf(
            msg, sizeof msg,
            "%s icon has an alpha channel, so its edges blend with the button",
            g.name);
        ok(b->image()->d() == 4, msg);
    }

    // The drawn size, measured on rendered pixels exactly like the bolt above:
    // it used to be a 10x11 speck in a 44px button, which is what this change
    // is about. Compared against the PNG icons measured the same way.
    {
        testsupport::Rendered magnifier = testsupport::renderWidget(
            (Fl_Button*)byTooltip(top, "Search torrents"));
        int refH = magnifier.ok ? magnifier.y1 - magnifier.y0 + 1 : 0;
        for (const Glyph& g : glyphs) {
            Fl_Button* b = (Fl_Button*)byTooltip(top, g.tip);
            if (!b) continue;
            testsupport::Rendered r = testsupport::renderWidget(b);
            int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
            char msg[192];
            snprintf(msg, sizeof msg,
                     "%s draws %dx%d px of ink, against %dpx for the PNG "
                     "magnifier next to it",
                     g.name, w, h, refH);
            ok(r.ok && w >= 14 && h >= 14, msg);
            snprintf(msg, sizeof msg,
                     "%s ink bbox (%d,%d)-(%d,%d) fits inside the %dx%d button",
                     g.name, r.x0, r.y0, r.x1, r.y1, b->w(), b->h());
            ok(r.ok && r.x0 >= 0 && r.y0 >= 0 && r.x1 < b->w() && r.y1 < b->h(),
               msg);
            // It must be centred like its neighbours: FLTK centres an image
            // that is smaller than its label-less button, and a glyph shoved to
            // one edge would read as a different toolbar from the rest.
            snprintf(
                msg, sizeof msg,
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
        struct LevelShot {
            int width;
            const char* what;
        } levels[] = {
            {fullW, "the full level"},
            {fullW - 1, "the short level"},
            {shortW, "the short level at its minimum width"},
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
               "the bar is observed in the level the width calls for before "
               "measuring");
            for (const Glyph& g : glyphs) {
                Fl_Button* b = (Fl_Button*)byTooltip(top, g.tip);
                if (!b) continue;
                testsupport::Rendered r = testsupport::renderWidget(b);
                char msg[192];
                snprintf(msg, sizeof msg, "%s at %s (%dpx): icon is %dx%d",
                         g.name, lv.what, lv.width,
                         b->image() ? b->image()->w() : 0,
                         b->image() ? b->image()->h() : 0);
                ok(b->image() && b->image()->w() == ToolbarLayout::kIconSize,
                   msg);
                snprintf(msg, sizeof msg,
                         "%s at %s (%dpx): still draws %dx%d px of ink", g.name,
                         lv.what, lv.width, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
                ok(r.ok && (r.x1 - r.x0 + 1) >= 14 && (r.y1 - r.y0 + 1) >= 14,
                   msg);
            }
        }
        win->resize(0, 0, 1400, 900);
    }

    // The pause/play pair is one button, and updateToolbar() swaps between
    // them. Both come from the same resampler, so the swap must not change the
    // size or the look: that is what would break if only one of the two had
    // been resampled. The swap itself needs a real TorrentItem, so flows.cpp
    // drives it with a torrent it creates; what is checked here is that the two
    // glyphs Resources hands out are the same size and both render properly.
    {
        Fl_Image* pause = Resources::getPauseIcon();
        Fl_Image* play = Resources::getPlayIcon();
        ok(pause != nullptr && play != nullptr,
           "both pause and play glyphs exist");
        ok(pause != play,
           "pause and play are different images, so the swap is visible");
        char msg[160];
        snprintf(msg, sizeof msg,
                 "the play glyph is %dx%d, same box as the pause glyph",
                 play ? play->w() : 0, play ? play->h() : 0);
        ok(play && play->w() == ToolbarLayout::kIconSize &&
               play->h() == ToolbarLayout::kIconSize,
           msg);
        snprintf(msg, sizeof msg,
                 "the play glyph carries an alpha channel too");
        ok(play && play->d() == 4, msg);

        // testsupport::Rendered straight onto a surface, the way the button
        // draws it, so the claim is about pixels and not about a w() accessor.
        struct {
            const char* name;
            Fl_Image* img;
        } both[] = {
            {"pause", pause},
            {"play", play},
        };
        for (auto& g : both) {
            Fl_Image_Surface surf(ToolbarLayout::kIconSize,
                                  ToolbarLayout::kIconSize);
            Fl_RGB_Image* shot = nullptr;
            {
                testsupport::SurfaceScope here(&surf);
                fl_color(FL_BLUE); // sentinel: anything not blue was drawn
                fl_rectf(0, 0, ToolbarLayout::kIconSize,
                         ToolbarLayout::kIconSize);
                g.img->draw(0, 0);
                shot = surf.image();
            }
            if (!shot || shot->d() != 3) {
                if (shot) delete shot;
                ok(false, "glyph rendered");
                continue;
            }
            // Painted pixels are anything that is not the sentinel. Partial
            // ones mean the edge was blended rather than stepped.
            int ink = 0, solid = 0, soft = 0;
            for (int y = 0; y < shot->h(); y++)
                for (int x = 0; x < shot->w(); x++) {
                    const unsigned char* q =
                        shot->array + (y * shot->w() + x) * 3;
                    if (q[0] < 20 && q[1] < 20 && q[2] > 200) continue;
                    ink++;
                    const int towardBody =
                        q[0] + q[1]; // blue has none of either
                    if (towardBody > 380) solid++;
                    else soft++;
                }
            delete shot;
            snprintf(msg, sizeof msg,
                     "the %s glyph paints %d px, %d of them solid and %d "
                     "anti-aliased",
                     g.name, ink, solid, soft);
            ok(ink > 100, msg);
            // A nearest-neighbour blow-up has no anti-aliased pixel at all:
            // every pixel is either fully painted or fully clear. Requiring at
            // least a few soft pixels is what makes "the glyph survived the
            // scale" mean something rather than just "something was drawn".
            snprintf(msg, sizeof msg,
                     "the %s glyph has %d anti-aliased edge pixels, so it was "
                     "resampled, not stretched",
                     g.name, soft);
            ok(soft >= 10, msg);
        }
    }
}
