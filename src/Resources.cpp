#include "Resources.h"
#include <FL/Fl_PNG_Image.H>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The glyphs are resampled to the size the toolbar draws icons at, so the bar
// has one idea of how big an icon is. Pulling the number from ToolbarLayout
// rather than repeating it is what keeps that idea in a single place.
#include "ToolbarLayout.h"

namespace {

// Resamples one compiled-in XPM glyph to `size` x `size`.
//
// The XPMs are 16x16 with hard edges, and Fl_Pixmap::copy() -- the obvious way
// to grow one -- is nearest neighbour. On this art that is visibly wrong: the
// pause bars are 4x11, so nearest neighbour turns them into 7x18 blocks and
// the remove cross, whose strokes are 2px, into a staircase. This averages the
// area each source pixel contributes to each destination pixel and carries the
// coverage in the alpha channel instead, so the shape survives the scale and
// the edges come out smooth.
//
// It lives here rather than at the call site because Resources owns what is
// compiled into the binary: whoever owns the glyphs owns how they are drawn.
Fl_RGB_Image* rescaleGlyph(const char* const* xpm, int size) {
    int w = 0, h = 0, colours = 0, chars = 0;
    if (std::sscanf(xpm[0], "%d %d %d %d", &w, &h, &colours, &chars) != 4) return nullptr;
    // One character per pixel and #RRGGBB colours is the shape every glyph in
    // Icons.h has. Anything else is refused here rather than mis-drawn: the
    // caller already handles a null icon, and a wrong picture would not.
    if (w <= 0 || h <= 0 || colours <= 0 || chars != 1 || size <= 0) return nullptr;

    // Which characters paint, and in what colour. "None" is the absence of
    // paint; a named FLTK colour is not something this parser can reproduce, so
    // it refuses instead of guessing.
    bool opaque[256] = {};
    unsigned char tint[256][3] = {};
    for (int i = 0; i < colours; i++) {
        const char* line = xpm[1 + i];
        const unsigned char key = static_cast<unsigned char>(line[0]);
        const char* hex = std::strchr(line, '#');
        if (std::strstr(line, "None")) {
            opaque[key] = false;
        } else if (hex && std::strlen(hex) >= 7) {
            unsigned value = 0;
            if (std::sscanf(hex + 1, "%6x", &value) != 1) return nullptr;
            opaque[key] = true;
            tint[key][0] = static_cast<unsigned char>((value >> 16) & 0xff);
            tint[key][1] = static_cast<unsigned char>((value >> 8) & 0xff);
            tint[key][2] = static_cast<unsigned char>(value & 0xff);
        } else {
            return nullptr;
        }
    }

    // Flatten the glyph to one premultiplied source pixel per grid cell.
    const int cells = w * h;
    std::vector<double> cover(cells, 0.0);
    std::vector<unsigned char> body(cells * 3, 0);
    for (int y = 0; y < h; y++) {
        const char* row = xpm[1 + colours + y];
        for (int x = 0; x < w; x++) {
            const unsigned char key = static_cast<unsigned char>(row[x]);
            if (!opaque[key]) continue;
            const int cell = y * w + x;
            cover[cell] = 1.0;
            body[cell * 3 + 0] = tint[key][0];
            body[cell * 3 + 1] = tint[key][1];
            body[cell * 3 + 2] = tint[key][2];
        }
    }

    // Box filter: for every destination pixel, sum what the source pixels it
    // overlaps contribute. The area each one contributes is the fraction of
    // the destination pixel it actually covers, so a 1.6x scale spreads a
    // hard edge over the two destination pixels it straddles instead of
    // picking one of them and doubling it.
    //
    // The buffer has to outlive the image: both Fl_RGB_Image constructors that
    // take raw pixels only ALIAS what they are given (alloc_array stays 0), so
    // a buffer on this function's stack would leave the image pointing at freed
    // memory. Hence new[] plus the documented ownership flag rather than a
    // local array.
    unsigned char* out = new unsigned char[static_cast<size_t>(size) * size * 4]();
    const double stepX = static_cast<double>(w) / size;
    const double stepY = static_cast<double>(h) / size;
    for (int dy = 0; dy < size; dy++) {
        const double top = dy * stepY, bottom = (dy + 1) * stepY;
        for (int dx = 0; dx < size; dx++) {
            const double left = dx * stepX, right = (dx + 1) * stepX;
            double total = 0.0, ink = 0.0, r = 0.0, g = 0.0, b = 0.0;
            for (int sy = static_cast<int>(top); sy <= static_cast<int>(std::ceil(bottom)) && sy < h; sy++) {
                if (sy < 0) continue;
                const double sliceY = std::min(bottom, sy + 1.0) - std::max(top, static_cast<double>(sy));
                if (sliceY <= 0.0) continue;
                for (int sx = static_cast<int>(left); sx <= static_cast<int>(std::ceil(right)) && sx < w; sx++) {
                    if (sx < 0) continue;
                    const double sliceX = std::min(right, sx + 1.0) - std::max(left, static_cast<double>(sx));
                    if (sliceX <= 0.0) continue;
                    const double share = sliceX * sliceY;
                    const int cell = sy * w + sx;
                    total += share;
                    ink += share * cover[cell];
                    r += share * cover[cell] * body[cell * 3 + 0];
                    g += share * cover[cell] * body[cell * 3 + 1];
                    b += share * cover[cell] * body[cell * 3 + 2];
                }
            }
            if (total <= 0.0) continue;
            unsigned char* px = &out[(static_cast<size_t>(dy) * size + dx) * 4];
            px[3] = static_cast<unsigned char>(std::lround(255.0 * ink / total));
            // Undo the premultiplication: a partly covered pixel keeps the
            // glyph's own colour and shows it through its alpha, which is what
            // an anti-aliased edge is.
            if (ink > 0.0) {
                px[0] = static_cast<unsigned char>(std::lround(r / ink));
                px[1] = static_cast<unsigned char>(std::lround(g / ink));
                px[2] = static_cast<unsigned char>(std::lround(b / ink));
            }
        }
    }

    // Depth 4 is what carries the alpha: FLTK blends those pixels with the
    // surface underneath, which is what makes a soft edge read as an edge
    // rather than as a staircase.
    Fl_RGB_Image* image = new Fl_RGB_Image(out, size, size, 4);
    image->alloc_array = 1;   // the image now owns `out` and frees it
    return image;
}

}  // namespace

// Static member initialization
Fl_RGB_Image* Resources::s_logo = nullptr;
Fl_Pixmap* Resources::s_iconAdd = nullptr;
Fl_Image* Resources::s_iconPause = nullptr;
Fl_Image* Resources::s_iconPlay = nullptr;
Fl_Image* Resources::s_iconRemove = nullptr;
Fl_Pixmap* Resources::s_iconSettings = nullptr;
Fl_Pixmap* Resources::s_iconDownload = nullptr;
Fl_Pixmap* Resources::s_iconUpload = nullptr;
Fl_Image* Resources::s_iconSearch = nullptr;

void Resources::initialize() {
    // Create logo from PNG memory buffer
    s_logo = new Fl_PNG_Image("logo.png", ftorrent_logo_png, ftorrent_logo_png_size);
    s_iconAdd = new Fl_Pixmap(icon_add_xpm);
    // These are the glyphs a button carries on its own, so they are the ones
    // resampled to the icon size. Search earns its place here despite normally
    // being a PNG: it is the fallback for the search button when that PNG is
    // missing, and a 16px glyph next to a 26px pause is the contrast this
    // resampling exists to remove. The rest sit beside a text label, where a
    // smaller glyph is the intended look, so they keep their own size.
    s_iconPause = rescaleGlyph(icon_pause_xpm, ToolbarLayout::kIconSize);
    s_iconPlay = rescaleGlyph(icon_play_xpm, ToolbarLayout::kIconSize);
    s_iconRemove = rescaleGlyph(icon_remove_xpm, ToolbarLayout::kIconSize);
    s_iconSearch = rescaleGlyph(icon_search_xpm, ToolbarLayout::kIconSize);
    s_iconSettings = new Fl_Pixmap(icon_settings_xpm);
    s_iconDownload = new Fl_Pixmap(icon_download_xpm);
    s_iconUpload = new Fl_Pixmap(icon_upload_xpm);
}

void Resources::cleanup() {
    delete s_logo;
    delete s_iconAdd;
    delete s_iconPause;
    delete s_iconPlay;
    delete s_iconRemove;
    delete s_iconSettings;
    delete s_iconDownload;
    delete s_iconUpload;
    delete s_iconSearch;
    
    s_logo = nullptr;
    s_iconAdd = nullptr;
    s_iconPause = nullptr;
    s_iconPlay = nullptr;
    s_iconRemove = nullptr;
    s_iconSettings = nullptr;
    s_iconDownload = nullptr;
    s_iconUpload = nullptr;
    s_iconSearch = nullptr;
}

Fl_RGB_Image* Resources::getLogoImage() {
    return s_logo;
}

Fl_Pixmap* Resources::getAddIcon() {
    return s_iconAdd;
}

Fl_Image* Resources::getPauseIcon() {
    return s_iconPause;
}

Fl_Image* Resources::getPlayIcon() {
    return s_iconPlay;
}

Fl_Image* Resources::getRemoveIcon() {
    return s_iconRemove;
}

Fl_Pixmap* Resources::getSettingsIcon() {
    return s_iconSettings;
}

Fl_Pixmap* Resources::getDownloadIcon() {
    return s_iconDownload;
}

Fl_Pixmap* Resources::getUploadIcon() {
    return s_iconUpload;
}

Fl_Image* Resources::getSearchIcon() {
    return s_iconSearch;
}
