#ifndef FTORRENT_TOOLBARLAYOUT_H
#define FTORRENT_TOOLBARLAYOUT_H

// How the toolbar arranges itself: which level of label detail fits in the
// space available, how wide each button is at that level, and what to do with
// the room going spare. Pure policy — it knows widths and labels, nothing about
// widgets. MainWindow asks and applies, so the numbers live in one place.
//
// It is its own module because this is the part that used to break silently:
// the space the toolbar needed was hand-summed next to the code that resized
// the buttons, so changing one button's width meant editing a total that
// nothing checked. Everything here derives from one table instead.
//
// Spare width: a level is never stretched past kMaxGrow per labelled button.
// Whatever is left after that becomes the equal margin that centres the block,
// so the bar uses the whole window without any single button running away.
//
// Centring needs a spacer at BOTH ends. Fl_Pack shrinks itself to the width of
// its children, so a pack with only a leading spacer shrinks, sits at the left
// of the window and has its content centred inside itself rather than in the
// window. With a trailing spacer as well, content plus the two margins add up
// to the window width, the pack stays full width, and the block lands in the
// middle.
//
// A spacer with no width still has to stay VISIBLE. Fl_Pack skips a hidden
// child together with the gap on either side of it, so hiding a collapsed
// spacer quietly takes 2 * kSpacing out of the row: the pack comes up short, the
// two margins cannot be equal, and at the narrowest width the window allows the
// block ends up against the left edge. requiredWidth() counts that gap, so a
// collapsed spacer stays in the pack and simply draws nothing.
//
// That leaves the spare width to spend, and it has to be spent exactly:
// kGrowableCount grown buttons plus two equal margins, with no pixel left over
// at the end of the bar. growPerButton() and margin() are the two halves of
// that one sum, so they have to agree on where it lands.

struct ToolbarLayout {

    enum Level {
        ICONS = 0,   // no room for any text
        SHORT = 1,   // shortened labels
        FULL = 2,    // everything spelled out
    };

    // The buttons whose width and label change with the level, plus the gap
    // between the groups. Everything else keeps a fixed width and is not
    // listed here.
    struct Spec {
        const char* addLabel;
        int addWidth;
        const char* createLabel;
        int createWidth;
        const char* prefsLabel;
        int prefsWidth;
        int spacerWidth;   // 0 means the spacer is hidden
    };

    // The buttons that carry text and may widen to use spare room. Icon-only
    // buttons are deliberately not among them: a wide button holding a single
    // glyph just looks empty.
    static constexpr int kGrowableCount = 3;

    // Ceiling on how far a labelled button may grow, so that widening the
    // window never turns a button into a slab.
    static constexpr int kMaxGrow = 40;

    // Widths of the widgets that never change with the level. The limit button
    // is here, not in the table: it carries an icon only, so it is the same
    // icon-sized button at every level.
    static constexpr int kIconButton = 46;    // add, create, pause, remove, search, limit
    static constexpr int kThemeButton = 34;
    static constexpr int kRamChoice = 60;

    // The bar and the button inside it are two different numbers, on purpose:
    // the bar is the strip, the buttons sit inside it with room above and below.
    //
    // Fl_Pack stretches every child to the height of the PACK, so the thing that
    // has to be kButtonHeight is the pack itself; kBarHeight is the strip it
    // sits in. Conflating the two is what made the buttons touch the bar's edge.
    static constexpr int kBarHeight = 44;
    static constexpr int kButtonHeight = 34;

    // Room above and below a button inside the bar. Derived from the two heights
    // rather than written down, so the gap cannot drift out of step with either.
    static constexpr int kBarPadding = (kBarHeight - kButtonHeight) / 2;

    // Edge length of a square toolbar icon. The PNGs are loaded at this size by
    // AssetLoader and the compiled-in glyphs are resampled to it by Resources,
    // so the whole bar has one idea of how big an icon is.
    static constexpr int kIconSize = 26;

    // Fl_Pack puts kSpacing between the kChildCount children. The first and the
    // last are the elastic spacers that centre the block.
    static constexpr int kSpacing = 5;

    // The row's children, counted rather than typed in beside the table. The
    // groups are the same terms requiredWidth() sums, one widget each, so the
    // count cannot drift from the arithmetic that depends on it:
    //
    //   3 labelled buttons      -- the three width fields of a Spec
    //   4 fixed icon buttons    -- pause, remove, search, limit: 4 * kIconButton
    //   2 spacers between groups-- the two spacerWidth entries of a Spec
    //   2 other fixed widgets   -- kThemeButton and kRamChoice, one each
    //   2 elastic spacers       -- the centring pair at the two ends
    //
    // A new toolbar widget therefore has to be added to one of these groups,
    // and kChildCount -- and with it every gap count -- follows it.
    static constexpr int kLabelledButtons = 3;
    static constexpr int kIconButtons = 4;
    static constexpr int kGroupSpacers = 2;
    static constexpr int kOtherFixedWidgets = 2;
    static constexpr int kElasticSpacers = 2;
    static constexpr int kChildCount = kLabelledButtons + kIconButtons
                                     + kGroupSpacers + kOtherFixedWidgets
                                     + kElasticSpacers;

    // True when a spacer of this width must stay in the pack.
    //
    // Fl_Pack skips a hidden child together with the gap on either side of it,
    // and requiredWidth() counts that gap for every child, so a spacer that has
    // collapsed to no width still has to be there. This is the rule the caller
    // obeys rather than repeats: the arithmetic that needs it lives here, and
    // the answer does too, without FLTK coming near this module.
    static bool spacerStaysVisible(int width);

    static const Spec& spec(Level level);

    // Room the level needs at its natural widths: its buttons plus the packing
    // gaps, with the two centring spacers counted at their smallest, which is
    // zero width but still a visible child, so still a gap on either side.
    static int requiredWidth(Level level);

    // The most detailed level that fits in `available` pixels.
    static Level forWidth(int available);

    // Pixels of the spare width each labelled button may take. Capped, so past
    // kMaxGrow the remainder becomes margin instead of more stretch. Grown by
    // one less than the fair share whenever that share would leave an odd pixel
    // over: two equal margins cannot swallow an odd pixel, and an unswallowed
    // one is dead bar at the end of the row.
    static int growPerButton(int slack);

    // The width of each centring spacer. The visible gap between the window
    // edge and the first button is this plus the pack's own kSpacing, because
    // Fl_Pack puts a gap between the spacer and the button next to it; it is
    // the same on both ends, which is what makes the block look centred.
    //
    // growPerButton(slack) * kGrowableCount + 2 * margin(slack) == slack, so
    // the row the pack builds is exactly as wide as the window it was given.
    static int margin(int slack);
};

#endif  // FTORRENT_TOOLBARLAYOUT_H
