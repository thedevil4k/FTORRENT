#include "ToolbarLayout.h"

// The one place the toolbar's numbers are written down. requiredWidth() reads
// these same values, so a button cannot be resized without the level that
// contains it following.
namespace {

const ToolbarLayout::Spec kTable[3] = {
    // ICONS
    {nullptr,           ToolbarLayout::kIconButton,
     nullptr,           ToolbarLayout::kIconButton,
     nullptr,           ToolbarLayout::kIconButton,
     0},
    // SHORT
    {" Add",            100,
     " Create",         105,
     "Prefs",           85,
     8},
    // FULL
    {" Add Torrent",    140,
     " Create Torrent", 140,
     "Preferences",     130,
     20},
};

}  // namespace

const ToolbarLayout::Spec& ToolbarLayout::spec(Level level) {
    return kTable[level];
}

int ToolbarLayout::requiredWidth(Level level) {
    const Spec& s = spec(level);
    // The listed buttons and the two spacers, then the six widgets that keep
    // their width at every level: pause, remove, search and limit share
    // kIconButton, the theme button and the RAM choice have their own.
    //
    // The gaps are kChildCount - 1 of them because every one of those children
    // is in the pack at every level: Fl_Pack skips a hidden child and the gap
    // beside it, so a spacer that has collapsed to zero width stays visible
    // (see the header) rather than quietly shortening the row that this number
    // is measuring.
    return s.addWidth + s.createWidth + s.prefsWidth + kGroupSpacers * s.spacerWidth
         + kIconButtons * kIconButton + kThemeButton + kRamChoice
         + kSpacing * (kChildCount - 1);
}

ToolbarLayout::Level ToolbarLayout::forWidth(int available) {
    // Most detailed first: the first level that fits is the one to use.
    if (available >= requiredWidth(FULL)) return FULL;
    if (available >= requiredWidth(SHORT)) return SHORT;
    return ICONS;
}

bool ToolbarLayout::spacerStaysVisible(int width) {
    // Every width, zero included. The width is the argument rather than a
    // constant so the rule reads as a question about the widget it is asked
    // about, and so a caller cannot quietly pass something the pack would treat
    // as a real gap.
    (void)width;
    return true;
}

int ToolbarLayout::growPerButton(int slack) {
    if (slack <= 0) return 0;
    const int fair = slack / kGrowableCount;
    int grow = fair < kMaxGrow ? fair : kMaxGrow;

    // Slack has to be spent, not just shared: kGrowableCount grown buttons plus
    // two equal margins, and two margins can only be an even number of pixels.
    // So when the fair share would leave an odd pixel over, the buttons give
    // one back and it becomes margin. Rounding it away instead costs nothing
    // visible at wide widths -- one dead pixel out of hundreds -- but at 720 it
    // was the pixel that kept the block from sitting centred in its own bar.
    if ((slack - grow * kGrowableCount) % 2) grow--;
    return grow > 0 ? grow : 0;
}

int ToolbarLayout::margin(int slack) {
    // Reads the growth back through growPerButton() rather than recomputing the
    // share, so the two halves of the sum cannot drift apart. Whatever is left
    // is even by then; the guard only bites on the one slack that no pair of
    // widths can express, a single spare pixel with no button to widen.
    const int left = slack - growPerButton(slack) * kGrowableCount;
    return left > 0 ? left / 2 : 0;
}
