#include "SecurityBadge.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

bool darkBackground() {
    uchar red = 255, green = 255, blue = 255;
    Fl::get_color(FL_BACKGROUND2_COLOR, red, green, blue);
    return red < 128;
}

bool bidiSequenceAt(const std::string& text, std::size_t pos,
                    std::size_t& length) {
    // Bidi controls and line/paragraph separators that can reorder or split a
    // name visually: U+200E/U+200F, U+202A..U+202E, U+2028/U+2029,
    // U+2066..U+2069 and U+061C.
    static const unsigned char sequences[][3] = {
        {0xE2, 0x80, 0x8E}, {0xE2, 0x80, 0x8F}, {0xE2, 0x80, 0xAA},
        {0xE2, 0x80, 0xAB}, {0xE2, 0x80, 0xAC}, {0xE2, 0x80, 0xAD},
        {0xE2, 0x80, 0xAE}, {0xE2, 0x80, 0xA8}, {0xE2, 0x80, 0xA9},
        {0xE2, 0x81, 0xA6}, {0xE2, 0x81, 0xA7}, {0xE2, 0x81, 0xA8},
        {0xE2, 0x81, 0xA9}
    };
    if (pos + 3 > text.size()) return false;
    for (const auto& sequence : sequences) {
        if (static_cast<unsigned char>(text[pos]) == sequence[0] &&
            static_cast<unsigned char>(text[pos + 1]) == sequence[1] &&
            static_cast<unsigned char>(text[pos + 2]) == sequence[2]) {
            length = 3;
            return true;
        }
    }
    if (pos + 2 <= text.size() &&
        static_cast<unsigned char>(text[pos]) == 0xD8 &&
        static_cast<unsigned char>(text[pos + 1]) == 0x9C) {
        length = 2;
        return true;
    }
    return false;
}

} // namespace

Fl_Color SecurityBadge::riskColor(RiskLevel level) {
    return riskColor(level, darkBackground());
}

Fl_Color SecurityBadge::riskColor(RiskLevel level, bool darkMode) {
    switch (level) {
        case RiskLevel::Low:
            return darkMode ? fl_rgb_color(135, 155, 165)
                            : fl_rgb_color(90, 110, 120);
        case RiskLevel::Medium:
            return darkMode ? fl_rgb_color(235, 175, 65)
                            : fl_rgb_color(190, 130, 20);
        case RiskLevel::High:
            return darkMode ? fl_rgb_color(245, 115, 70)
                            : fl_rgb_color(210, 80, 40);
        case RiskLevel::Critical:
            return darkMode ? fl_rgb_color(245, 75, 85)
                            : fl_rgb_color(180, 30, 40);
        case RiskLevel::Unknown:
        default:
            return darkMode ? fl_rgb_color(150, 150, 150)
                            : fl_rgb_color(120, 120, 120);
    }
}

Fl_Color SecurityBadge::outlineColor(bool darkMode) {
    return darkMode ? fl_rgb_color(245, 245, 245)
                    : fl_rgb_color(35, 35, 35);
}

std::string SecurityBadge::riskLabel(RiskLevel level) {
    switch (level) {
        case RiskLevel::Low:      return "Low risk";
        case RiskLevel::Medium:   return "Medium risk";
        case RiskLevel::High:     return "High risk";
        case RiskLevel::Critical: return "Critical risk";
        case RiskLevel::Unknown:
        default:                  return "Unknown";
    }
}

bool SecurityBadge::hasWarning(RiskLevel level) {
    return level == RiskLevel::High || level == RiskLevel::Critical;
}

std::string SecurityBadge::displayText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20 || byte == 0x7f) {
            out.push_back('?');
            ++i;
            continue;
        }
        std::size_t length = 0;
        if (bidiSequenceAt(text, i, length)) {
            out.push_back('?');
            i += length;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

void SecurityBadge::drawShield(int x, int y, int w, int h, RiskLevel level) {
    if (w < 4 || h < 4) return;

    const bool dark = darkBackground();
    const Fl_Color fill = riskColor(level, dark);
    const Fl_Color outline = outlineColor(dark);

    const double left = x + 1.0;
    const double right = x + w - 1.0;
    const double top = y + 1.0;
    const double upper = y + h * 0.28;
    const double middle = y + h * 0.56;
    const double bottom = y + h - 1.0;
    const double centre = x + w / 2.0;

    fl_color(fill);
    fl_begin_polygon();
    fl_vertex(centre, top);
    fl_vertex(right, upper);
    fl_vertex(right, middle);
    fl_vertex(centre, bottom);
    fl_vertex(left, middle);
    fl_vertex(left, upper);
    fl_end_polygon();

    fl_color(outline);
    fl_begin_loop();
    fl_vertex(centre, top);
    fl_vertex(right, upper);
    fl_vertex(right, middle);
    fl_vertex(centre, bottom);
    fl_vertex(left, middle);
    fl_vertex(left, upper);
    fl_end_loop();

    if (level == RiskLevel::Unknown || level == RiskLevel::High ||
        level == RiskLevel::Critical) {
        fl_color(FL_WHITE);
        fl_font(FL_HELVETICA_BOLD, std::max(8, h / 2));
        const char* glyph = level == RiskLevel::Unknown ? "?" : "!";
        fl_draw(glyph, x, y, w, h, FL_ALIGN_CENTER);
    }
}

SecurityShieldBox::SecurityShieldBox(int x, int y, int w, int h,
                                     const char* label)
    : Fl_Box(x, y, w, h, label) {
    box(FL_NO_BOX);
    align(FL_ALIGN_CENTER);
}

void SecurityShieldBox::setRisk(RiskLevel level, const std::string& tooltip) {
    m_level = level;
    if (tooltip.empty()) {
        this->tooltip(nullptr);
    } else {
        copy_tooltip(tooltip.c_str());
    }
    redraw();
}

void SecurityShieldBox::draw() {
    SecurityBadge::drawShield(x() + 1, y() + 1, w() - 2, h() - 2, m_level);
}

int SecurityShieldBox::handle(int event) {
    if (!m_onClick) return Fl_Box::handle(event);

    switch (event) {
        case FL_PUSH:
            // Claimed so the click does not fall through to the label beside
            // the shield, which would look like the whole row reacted.
            return 1;
        case FL_RELEASE:
            if (Fl::event_inside(this)) {
                m_onClick();
                return 1;
            }
            return 0;
        case FL_ENTER:
            if (window()) window()->cursor(FL_CURSOR_HAND);
            return 1;
        case FL_LEAVE:
            if (window()) window()->cursor(FL_CURSOR_DEFAULT);
            return 1;
        default:
            break;
    }
    return Fl_Box::handle(event);
}
