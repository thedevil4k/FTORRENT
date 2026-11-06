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
#undef B0
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
#include "SecurityBadge.h"
#include "test_support.h"
#include "icons_support.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

static Fl_Window* g_main = nullptr;

namespace {

using iconsupport::byTooltip;
using iconsupport::inkOfShield;
using iconsupport::isMagenta;
using testsupport::ok;

} // namespace

int main(int argc, char** argv) {
    std::string mode = (argc > 1) ? argv[1] : "full";
    printf("  [assets=%s]\n", mode.c_str());

    testsupport::useScratchConfig("icons");

    Fl::lock();
    Resources::initialize();
    SettingsManager::instance().load();

    auto manager = std::make_unique<TorrentManager>();
    if (!manager->initialize()) {
        printf("  TorrentManager::initialize() failed\n");
        return 3;
    }

    MainWindow* win = new MainWindow(1400, 900, "FTorrent");
    g_main = win;
    win->setTorrentManager(manager.get());
    win->show();

    Fl_Widget* top = Fl::first_window();
    if (!top) {
        printf("  no window\n");
        return 3;
    }

    Fl_Button* search = (Fl_Button*)byTooltip(top, "Search torrents");
    Fl_Button* limit = (Fl_Button*)byTooltip(top, "Limit network speed 50%");
    ok(search != nullptr, "search button found by tooltip");
    ok(limit != nullptr, "limit button found by tooltip");
    if (!search || !limit) {
        printf("  checks=%d failures=%d\n", testsupport::checks,
               testsupport::failures);
        manager->shutdown();
        Resources::cleanup();
        testsupport::removeScratchConfig("icons");
        return 1;
    }

    // Which picture the search button is drawing. Both variants put an icon of
    // the same box size there, so the box size cannot tell them apart and
    // neither can pointer identity: AssetLoader::load() returns a fresh copy
    // per call, so the button's image is never equal to anything loaded here.
    // What separates them is the drawn pixels, so that is what is compared.
    if (mode == "full") {
        ok(search->image() != nullptr, "search button carries an icon");
        ok(search->image() && search->image()->w() == ToolbarLayout::kIconSize,
           "search button icon is at the toolbar icon size");
        {
            std::unique_ptr<Fl_Image> fromDisk(
                AssetLoader::load("find.png", ToolbarLayout::kIconSize));
            ok(fromDisk != nullptr, "find.png is installed and decodes");
            ok(fromDisk &&
                   testsupport::samePixels(search->image(), fromDisk.get()),
               "search button draws the supplied PNG's pixels, not something "
               "else");
            ok(search->image() &&
                   !testsupport::samePixels(search->image(),
                                            Resources::getSearchIcon()),
               "search button is NOT falling back to the compiled-in "
               "magnifier");
        }
        ok(search->image() && !testsupport::samePixels(
                                  search->image(), Resources::getPauseIcon()),
           "the search icon is not the pause glyph");
        ok(limit->image() != nullptr, "limit button carries an icon");
        ok(limit->image() && limit->image()->w() == ToolbarLayout::kIconSize,
           "limit button icon is at the toolbar icon size");
        ok(search->image() &&
               !testsupport::samePixels(search->image(), limit->image()),
           "the two buttons do not share one picture");
        {
            // The badge the search rows draw when a torrent contains a
            // compressed package. Squared on purpose: it is stretched into the
            // same 15x15 box as the shield, so the tag group has one size
            // regardless of each PNG's aspect ratio.
            std::unique_ptr<Fl_Image> badge(
                AssetLoader::load("compressed.png", 15));
            ok(badge != nullptr, "compressed.png is installed and decodes");
            ok(badge && badge->w() == 15 && badge->h() == 15,
               "the archive badge fills the exact square shared with the "
               "shield");
        }
        {
            // The other content badges: disc image, video and native binary.
            // They share the shield's box, so each one has to decode and fill
            // exactly 15x15, never collapse to a blank square or a different
            // size that would make the group look ragged.
            struct BadgeAsset {
                const char* file;
                const char* what;
            };
            const BadgeAsset assets[] = {
                {"iso.png", "the disc-image badge is installed and decodes"},
                {"video.png", "the video badge is installed and decodes"},
                {"executable.png",
                 "the executable badge is installed and decodes"},
                {"encrypted.png",
                 "the password-protected badge is installed and decodes"},
            };
            for (const BadgeAsset& asset : assets) {
                std::unique_ptr<Fl_Image> badge(
                    AssetLoader::load(asset.file, 15));
                ok(badge != nullptr, asset.what);
                ok(badge && badge->w() == 15 && badge->h() == 15,
                   "and fills the exact square shared with the shield");
            }
        }
        {
            // What the row actually paints, which is the only thing the user
            // compares: the shield beside the name and every content badge next
            // to it must cover the same area. Measuring the decoded image is
            // not enough -- it would pass even when the art fills the box and
            // the shield, inset by its own outline, does not.
            const testsupport::InkBox shield = inkOfShield(15);
            ok(shield.ok, "the shield renders");
            if (shield.ok) {
                struct BadgeArt {
                    const char* file;
                    const char* what;
                };
                const BadgeArt arts[] = {
                    {"compressed.png",
                     "the archive badge is the shield's size"},
                    {"encrypted.png", "the lock badge is the shield's size"},
                    {"iso.png", "the disc-image badge is the shield's size"},
                    {"video.png", "the video badge is the shield's size"},
                    {"executable.png",
                     "the executable badge is the shield's size"},
                };
                for (const BadgeArt& art : arts) {
                    std::unique_ptr<Fl_Image> badge(
                        AssetLoader::load(art.file, 15));
                    if (!badge) continue; // covered by the checks above
                    const testsupport::InkBox ink = testsupport::inkOfImage(
                        badge->w(), badge->h(), badge.get());
                    // One pixel of slack: the shield's polygon is inset by its
                    // outline, so a badge with no transparent padding paints
                    // one row and one column more. Anything wider than that is
                    // the rag the group must not have.
                    ok(ink.ok && ink.w <= shield.w + 1 && ink.h <= shield.h + 1,
                       art.what);
                }
            }
        }
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
           "find.png really is absent, so this variant is exercising the "
           "fallback");
        ok(AssetLoader::load("compressed.png", 15) == nullptr,
           "the archive badge asset is absent too, so no blank square is "
           "drawn");
        ok(AssetLoader::load("iso.png", 15) == nullptr,
           "the disc-image badge asset is absent too");
        ok(AssetLoader::load("video.png", 15) == nullptr,
           "the video badge asset is absent too");
        ok(AssetLoader::load("executable.png", 15) == nullptr,
           "the executable badge asset is absent too");
        ok(AssetLoader::load("encrypted.png", 15) == nullptr,
           "the lock badge asset is absent too");
        ok(search->image() && testsupport::samePixels(
                                  search->image(), Resources::getSearchIcon()),
           "search button draws the compiled-in magnifier's pixels");
        ok(search->image() &&
               search->image()->w() == ToolbarLayout::kIconSize &&
               search->image()->h() == ToolbarLayout::kIconSize,
           "the fallback magnifier is at the toolbar icon size, like its "
           "neighbours");
        // A pixmap would be nearest-neighbour stretched, with no alpha to blend
        // the edge; this is the property that makes it read as the same family.
        ok(search->image() && search->image()->d() == 4,
           "the fallback magnifier is resampled, not a stretched 1-bit pixmap");
        ok(limit->image() == nullptr,
           "limit button has NO image rather than a blank one");
    }

    checkToolbarGeometry(win, top, mode);

    // --- the inactive background must be the toolbar's own, measured on the
    // pixels that actually get drawn, not on the colour attribute.
    auto neighbour = [&]() {
        return (Fl_Button*)byTooltip(top, "Add Torrent");
    };
    auto sameBackground = [&](const char* what) {
        testsupport::Rendered mine = testsupport::renderWidget(limit);
        testsupport::Rendered theirs = testsupport::renderWidget(neighbour());
        char msg[192];
        snprintf(msg, sizeof msg,
                 "%s: limit bg rgb(%u,%u,%u) == Add bg rgb(%u,%u,%u)", what,
                 mine.bg.r, mine.bg.g, mine.bg.b, theirs.bg.r, theirs.bg.g,
                 theirs.bg.b);
        ok(mine.ok && theirs.ok && mine.bg.r == theirs.bg.r &&
               mine.bg.g == theirs.bg.g && mine.bg.b == theirs.bg.b,
           msg);
    };

    sameBackground("dark theme, limit off");

    // --- the bolt is drawn big enough to read, and still inside the button
    if (mode == "full") {
        testsupport::Rendered bolt = testsupport::renderWidget(limit);
        ok(bolt.ok, "limit button rendered offscreen");
        int bw = bolt.x1 - bolt.x0 + 1, bh = bolt.y1 - bolt.y0 + 1;
        char msg[160];
        snprintf(msg, sizeof msg,
                 "bolt is drawn %dx%d px (it was 20x13 before the bar grew)",
                 bw, bh);
        ok(bw >= ToolbarLayout::kIconButton - 22 && bh >= 17, msg);
        snprintf(msg, sizeof msg,
                 "bolt bbox (%d,%d)-(%d,%d) fits the 40x30 button", bolt.x0,
                 bolt.y0, bolt.x1, bolt.y1);
        ok(bolt.x0 >= 0 && bolt.y0 >= 0 && bolt.x1 < limit->w() &&
               bolt.y1 < limit->h(),
           msg);
    }

    // --- the magenta active state
    ok(!isMagenta(limit), "limit button is not magenta while inactive");

    win->toggleNetworkLimit();
    ok(isMagenta(limit), "limit button turns magenta when the limit is on");
    ok(limit->label() == nullptr, "toggling the limit does not add text back");
    {
        testsupport::Rendered on = testsupport::renderWidget(limit);
        char msg[96];
        snprintf(msg, sizeof msg, "limit on renders bg rgb(%u,%u,%u)", on.bg.r,
                 on.bg.g, on.bg.b);
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

    printf("  assets=%-8s checks=%d failures=%d\n", mode.c_str(),
           testsupport::checks, testsupport::failures);
    manager->shutdown();
    Resources::cleanup();
    testsupport::removeScratchConfig("icons");
    return testsupport::failures == 0 ? 0 : 1;
}