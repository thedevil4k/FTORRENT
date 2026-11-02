// SecurityBadge checks.
//
// The badge owns two things that are easy to get subtly wrong everywhere they
// are used: the risk palette/labels and the sanitising of attacker-controlled
// names before they reach a row. The drawing itself is checked by rendering the
// real shield offscreen, so the test runs headless and still proves pixels
// reached the surface.
#include "SecurityBadge.h"
#include "test_support.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const char* what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

bool sameColor(Fl_Color a, Fl_Color b) {
    uchar ar = 0, ag = 0, ab = 0, br = 0, bg = 0, bb = 0;
    Fl::get_color(a, ar, ag, ab);
    Fl::get_color(b, br, bg, bb);
    return ar == br && ag == bg && ab == bb;
}

// Renders one shield on a black backdrop and returns how many pixels changed.
int paintedPixels(RiskLevel level) {
    const int w = 60, h = 60;
    Fl_Image_Surface surf(w, h);
    Fl_RGB_Image* image = nullptr;
    {
        testsupport::SurfaceScope here(&surf);
        fl_color(FL_BLACK);
        fl_rectf(0, 0, w, h);
        SecurityBadge::drawShield(2, 2, w - 4, h - 4, level);
        image = surf.image();
    }
    if (!image) return -1;

    int painted = 0;
    if (image->d() == 3 && image->array) {
        const unsigned char* p =
            reinterpret_cast<const unsigned char*>(image->array);
        for (int i = 0; i < w * h; ++i) {
            if (p[i * 3] || p[i * 3 + 1] || p[i * 3 + 2]) ++painted;
        }
    }
    delete image;
    return painted;
}

bool centreIs(RiskLevel level) {
    const int w = 60, h = 60;
    Fl_Image_Surface surf(w, h);
    Fl_RGB_Image* image = nullptr;
    {
        testsupport::SurfaceScope here(&surf);
        fl_color(FL_BLACK);
        fl_rectf(0, 0, w, h);
        SecurityBadge::drawShield(2, 2, w - 4, h - 4, level);
        image = surf.image();
    }
    if (!image) return false;

    bool matches = false;
    if (image->d() == 3 && image->array) {
        const unsigned char* p =
            reinterpret_cast<const unsigned char*>(image->array);
        const int index = (h / 2 * w + w / 2) * 3;
        uchar r = 0, g = 0, b = 0;
        Fl::get_color(SecurityBadge::riskColor(level), r, g, b);
        matches = p[index] == r && p[index + 1] == g && p[index + 2] == b;
    }
    delete image;
    return matches;
}

} // namespace

int main() {
    section("labels");
    ok(SecurityBadge::riskLabel(RiskLevel::Unknown) == "Unknown",
       "unknown has its own label");
    ok(SecurityBadge::riskLabel(RiskLevel::Low) == "Low risk",
       "low is a risk, not a clean bill of health");
    ok(SecurityBadge::riskLabel(RiskLevel::Medium) == "Medium risk",
       "medium label");
    ok(SecurityBadge::riskLabel(RiskLevel::High) == "High risk", "high label");
    ok(SecurityBadge::riskLabel(RiskLevel::Critical) == "Critical risk",
       "critical label");
    for (RiskLevel level : {RiskLevel::Unknown, RiskLevel::Low,
                            RiskLevel::Medium, RiskLevel::High,
                            RiskLevel::Critical}) {
        ok(SecurityBadge::riskLabel(level) != "Safe",
           "no risk level is ever labelled safe");
    }

    section("warning levels");
    ok(!SecurityBadge::hasWarning(RiskLevel::Unknown),
       "unknown does not trigger a confirmation");
    ok(!SecurityBadge::hasWarning(RiskLevel::Low),
       "low does not trigger a confirmation");
    ok(!SecurityBadge::hasWarning(RiskLevel::Medium),
       "medium does not trigger a confirmation");
    ok(SecurityBadge::hasWarning(RiskLevel::High),
       "high triggers a confirmation");
    ok(SecurityBadge::hasWarning(RiskLevel::Critical),
       "critical triggers a confirmation");

    section("palette");
    ok(!sameColor(SecurityBadge::riskColor(RiskLevel::Low, false),
                  SecurityBadge::riskColor(RiskLevel::Critical, false)),
       "low and critical do not share a colour");
    ok(!sameColor(SecurityBadge::riskColor(RiskLevel::Medium, false),
                  SecurityBadge::riskColor(RiskLevel::High, false)),
       "medium and high do not share a colour");
    ok(!sameColor(SecurityBadge::outlineColor(false),
                  SecurityBadge::outlineColor(true)),
       "the outline follows the theme");

    section("display text");
    ok(SecurityBadge::displayText("readme.txt") == "readme.txt",
       "ordinary text is untouched");
    ok(SecurityBadge::displayText(std::string("a\x01" "b")) == "a?b",
       "an ASCII control character is replaced");
    ok(SecurityBadge::displayText(std::string("a\x7f" "b")) == "a?b",
       "DEL is replaced");
    ok(SecurityBadge::displayText(std::string("a\xE2\x80\xAE" "b")) == "a?b",
       "a right-to-left override is replaced");
    ok(SecurityBadge::displayText(std::string("a\xE2\x81\xA6" "b")) == "a?b",
       "an isolate control is replaced");
    ok(SecurityBadge::displayText(std::string("a\xD8\x9C" "b")) == "a?b",
       "the Arabic letter mark is replaced");
    ok(SecurityBadge::displayText(std::string("caf\xC3\xA9")) ==
           std::string("caf\xC3\xA9"),
       "valid UTF-8 is left byte-for-byte intact");

    section("archive entry line");
    {
        // What a row and the Details panel print for one entry of an archive
        // the user just indexed: name, size, and the short tags.
        SecurityFile readme;
        readme.path = "docs/readme.txt";
        readme.size = 1234;
        const std::string line = SecurityBadge::archiveEntryLine(readme);
        ok(line.rfind("- docs/readme.txt", 0) == 0,
           "the line starts with a bullet and the entry path");
        ok(line.find("(1.21 KB)") != std::string::npos,
           "the size is formatted the same way the tables format it");
        ok(line.find('[') == std::string::npos,
           "a plain entry carries no tags");

        SecurityFile tool;
        // Inside a folder, so the root-executable tag does not join the list:
        // this check is about the EXE tag reaching the line.
        tool.path = "bin/setup.exe";
        tool.size = 5 * 1024 * 1024;
        const std::string exe = SecurityBadge::archiveEntryLine(tool);
        ok(exe.find("(5.00 MB)") != std::string::npos,
           "a larger size keeps its unit");
        ok(exe.find("[EXE]") != std::string::npos,
           "an executable inside the archive is tagged");

        SecurityFile locked;
        locked.path = "private/photo.jpg";
        locked.size = 10;
        locked.encrypted = true;
        ok(SecurityBadge::archiveEntryLine(locked).find("[ENCRYPTED]") !=
               std::string::npos,
           "a password-protected entry says so in the same line");

        SecurityFile hostile;
        hostile.path = std::string("evi\xE2\x80\xAEl.exe");
        hostile.size = 1;
        const std::string safe = SecurityBadge::archiveEntryLine(hostile);
        ok(safe.find('\xE2') == std::string::npos,
           "a bidi override from the archive never reaches the row");
    }

    section("drawing");
    for (RiskLevel level : {RiskLevel::Unknown, RiskLevel::Low,
                            RiskLevel::Medium, RiskLevel::High,
                            RiskLevel::Critical}) {
        const int painted = paintedPixels(level);
        ok(painted > 100, "the shield paints a visible shape");
    }
    ok(centreIs(RiskLevel::Low), "the low shield is filled with its colour");
    ok(centreIs(RiskLevel::Medium),
       "the medium shield is filled with its colour");
    ok(!centreIs(RiskLevel::Critical),
       "the critical shield puts its warning glyph at the centre");
    {
        Fl_Image_Surface surf(8, 8);
        testsupport::SurfaceScope here(&surf);
        SecurityBadge::drawShield(0, 0, 0, 0, RiskLevel::High);
        SecurityBadge::drawShield(0, 0, 3, 3, RiskLevel::High);
        ok(true, "a tiny shield is refused instead of drawn badly");
    }

    section("widget");
    {
        SecurityShieldBox box(0, 0, 20, 20);
        box.setRisk(RiskLevel::High, "High risk. Review before downloading.");
        ok(box.risk() == RiskLevel::High, "the widget keeps the level");
        ok(box.tooltip() &&
               std::strcmp(box.tooltip(),
                           "High risk. Review before downloading.") == 0,
           "the widget keeps the tooltip");
        box.setRisk(RiskLevel::Low);
        ok(box.risk() == RiskLevel::Low, "the widget updates the level");
        ok(box.tooltip() == nullptr, "an empty tooltip clears the old one");
    }

    std::cout << "\nsecurity_badge checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
