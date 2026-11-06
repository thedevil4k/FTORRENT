// The helpers the icon checks and the toolbar geometry checks share.
//
// The icons suite covers two things that used to live in one file: the
// pictures the buttons draw, and the geometry of the bar they sit in. The
// helpers both halves need are here, so neither copy has to own them.
#ifndef FTORRENT_TESTS_ICONS_SUPPORT_H
#define FTORRENT_TESTS_ICONS_SUPPORT_H

#include "test_support.h"

#include "ToolbarLayout.h"
#include "SecurityBadge.h"

#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>

#include <cstring>

class MainWindow;

// The geometry half, in icons_geometry.cpp.
void checkToolbarGeometry(MainWindow* win, Fl_Widget* top,
                          const std::string& mode);

namespace iconsupport {

inline Fl_Widget* byTooltip(Fl_Widget* w, const char* tip) {
    if (w->tooltip() && strcmp(w->tooltip(), tip) == 0) return w;
    if (Fl_Group* g = w->as_group())
        for (int i = 0; i < g->children(); i++)
            if (Fl_Widget* hit = byTooltip(g->child(i), tip)) return hit;
    return nullptr;
}

// Fl_Pack moves its children while it draws, so a headless probe sees the
// positions from the last real draw unless it lets one happen. Rendering into
// an offscreen surface is the same code path the window runs on every frame:
// that is what testsupport::forceLayout() does.

inline bool isMagenta(Fl_Button* b) {
    return b->color() == fl_rgb_color(255, 0, 255);
}

inline bool sameLabel(const char* got, const char* want) {
    if (!want) return got == nullptr;
    return got && strcmp(got, want) == 0;
}

// Which level the bar is ACTUALLY in, read back off the three labelled
// buttons. ToolbarLayout::forWidth() is only the policy's prediction of this,
// so a bar that failed to apply a level would leave the labels at the previous
// level's words; asking the widgets removes that from the blind spot.
inline bool observedLevel(Fl_Button* add, Fl_Button* create, Fl_Button* prefs,
                          ToolbarLayout::Level* out) {
    const char* got[3] = {
        (add && add->label()) ? add->label() : nullptr,
        (create && create->label()) ? create->label() : nullptr,
        (prefs && prefs->label()) ? prefs->label() : nullptr,
    };
    for (int lv = ToolbarLayout::ICONS; lv <= ToolbarLayout::FULL; lv++) {
        const ToolbarLayout::Spec& sp =
            ToolbarLayout::spec((ToolbarLayout::Level)lv);
        if (sameLabel(got[0], sp.addLabel) &&
            sameLabel(got[1], sp.createLabel) &&
            sameLabel(got[2], sp.prefsLabel)) {
            *out = (ToolbarLayout::Level)lv;
            return true;
        }
    }
    return false; // labels match no level at all: a mixed or stale bar
}

// Renders an image and compares the pixels. Which picture a button is drawing
// is a question about pixels, not about pointers: AssetLoader::load() returns a
// fresh copy on every call, so the object a button holds is never equal to
// anything the suite can load itself, and never equal to the compiled-in glyph.
// The probe itself lives in test_support.h, next to the other offscreen
// helpers.

inline const char* levelName(ToolbarLayout::Level lv) {
    return lv == ToolbarLayout::FULL    ? "FULL"
           : lv == ToolbarLayout::SHORT ? "SHORT"
                                        : "ICONS";
}

// The risk shield as the Name cell draws it: a 15 px square.
inline testsupport::InkBox inkOfShield(int side) {
    return testsupport::inkOfPaint(side + 8, [side] {
        SecurityBadge::drawShield(0, 0, side, side, RiskLevel::Medium);
    });
}

} // namespace iconsupport

#endif
