// Shared helpers for the FTorrent test programs.
//
// Every test here builds the REAL MainWindow and inspects the real widgets, so
// two things have to be true of the process before it does: the config it reads
// has to be one the test owns, and the widgets have to have been laid out by a
// real draw before their positions mean anything.
//
// Config: the app resolves its config through SystemUtils, which honours
// XDG_CONFIG_HOME on Linux and APPDATA on Windows. Pointing that at a scratch
// directory is what keeps a test run from reading -- or worse, overwriting --
// the settings of whoever is running it. The earlier version of these probes
// shelled out to `rm -rf ~/.config/ftorrent`, which is not something a test is
// allowed to do to the machine it runs on.
//
// Layout: Fl_Pack positions its children while it draws, not while it is built,
// so a headless process that never draws sees stale coordinates. Rendering into
// an offscreen surface runs the same code the window runs every frame, which is
// what makes the geometry assertions meaningful without a display.
#ifndef FTORRENT_TESTS_TEST_SUPPORT_H
#define FTORRENT_TESTS_TEST_SUPPORT_H

// <termios.h> defines B0 as the hangup baud rate, and libtorrent's headers
// reach it through boost.asio. FLTK 1.3's Fl_Paged_Device.H -- pulled in by
// <FL/Fl_Image_Surface.H> -- declares a Page_Format enum with B0 in it, so a
// translation unit that includes libtorrent first sends the compiler into that
// enum with B0 already a macro. FLTK 1.4 renamed those members, which is why
// this only bites against 1.3. Nothing here wants the baud constant.
#ifdef B0
#undef B0
#endif
#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace testsupport {

inline int checks = 0;
inline int failures = 0;

// No printf-format attribute here on purpose: it is a GCC/Clang extension and
// this project also builds with MSVC for Windows, where it will not compile.
// The format strings are checked by the compiler's ordinary argument checking.
inline void ok(bool cond, const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    checks++;
    if (!cond) {
        failures++;
        printf("    FAIL %s\n", msg);
    }
}

inline void section(const char* title) {
    printf("\n== %s ==\n", title);
}

// Mirrors SystemUtils::getConfigDir() exactly, per platform. It has to: the
// whole point is to create the directory the app will actually open, and the
// three platforms do not read the same variable.
//
//   Windows  %APPDATA%, resolved once by SHGetFolderPathA(CSIDL_APPDATA).
//            That call does NOT follow a later change to %APPDATA%, which is
//            why useScratchConfig() also sets FTORRENT_CONFIG_DIR.
//   macOS    $HOME/Library/Application Support/FTorrent -- and NOTHING else, so
//            XDG_CONFIG_HOME is ignored there
//   Linux    $XDG_CONFIG_HOME, falling back to $HOME/.config
//
// An explicit FTORRENT_CONFIG_DIR wins on every platform and is what the test
// helper actually relies on.
inline std::filesystem::path configDir() {
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    return std::filesystem::path(appdata ? appdata : ".") / "FTorrent";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return std::filesystem::path(home ? home : ".") / "Library" /
           "Application Support" / "FTorrent";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::filesystem::path(xdg) / "ftorrent";
    const char* home = getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "ftorrent";
#endif
}

inline std::filesystem::path scratchConfigDir(const char* name) {
    return std::filesystem::temp_directory_path() /
           ("ftorrent-test-" + std::string(name));
}

// Point the app's config at a scratch directory this process owns. Must run
// before SettingsManager is loaded, since that is when the path is resolved.
//
// Overwrites whatever was there, because the point is that THIS run owns the
// directory: a config left behind by an earlier run would let state leak from
// one test into the next. To keep the real one, set XDG_CONFIG_HOME yourself
// after calling this -- or do not call this at all and manage it outside.
inline void useScratchConfig(const char* name) {
    namespace fs = std::filesystem;
    fs::path dir = scratchConfigDir(name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
#if defined(_WIN32)
    _putenv_s("APPDATA", dir.string().c_str());
#elif defined(__APPLE__)
    // macOS resolves the config from $HOME only, so that is what has to move.
    // Redirecting HOME also keeps the downloads directory inside the scratch
    // tree, which is where a test wants it anyway.
    _putenv_s("HOME", dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
#endif

    // Create the directory the app will ACTUALLY use, not just the one named in
    // the environment variable: SystemUtils appends a suffix to it, and
    // SettingsManager::save() opens the file without creating a directory, so a
    // missing one turns every save into a silent no-op.
    //
    // The explicit override is also set: SHGetFolderPath() on Windows does not
    // follow a runtime change to APPDATA, so the suffix alone was not enough
    // and a test key could land in -- and later be removed from -- the real
    // profile.
    const std::string finalDir = configDir().string();
#if defined(_WIN32) || defined(__APPLE__)
    _putenv_s("FTORRENT_CONFIG_DIR", finalDir.c_str());
#else
    setenv("FTORRENT_CONFIG_DIR", finalDir.c_str(), 1);
#endif
    std::error_code ec2;
    std::filesystem::create_directories(finalDir, ec2);
}

// Remove the scratch directory, so a test run leaves nothing behind.
inline void removeScratchConfig(const char* name) {
    std::error_code ec;
    std::filesystem::remove_all(scratchConfigDir(name), ec);
}

// Draw into `surf`, and put the previous surface back when this scope ends.
//
// FLTK 1.4 renamed this pair of calls to Fl_Surface_Device::push_current() and
// pop_current(). FLTK 1.3 -- which is what Ubuntu 24.04, Debian and the Flatpak
// SDK all ship -- has no such members: its Fl_Surface_Device (declared in
// FL/Fl_Device.H) offers only the static surface() accessor and the virtual
// set_current(), and its own documentation describes the save/restore dance in
// exactly those terms. 1.4 kept them for back-compatibility, so this is the one
// spelling that compiles against both series -- which matters because the CI
// test job builds against 1.3 while a desktop build may be against 1.4.
class SurfaceScope {
public:
    explicit SurfaceScope(Fl_Surface_Device* surf)
        : previous_(Fl_Surface_Device::surface()) {
        surf->set_current();
    }
    ~SurfaceScope() {
        // 1.3 reports no surface until one exists; 1.4 creates one instead.
        if (previous_) previous_->set_current();
    }
    SurfaceScope(const SurfaceScope&) = delete;
    SurfaceScope& operator=(const SurfaceScope&) = delete;

private:
    Fl_Surface_Device* previous_;
};

// Let a container place its children the way the window does, by drawing it.
inline void forceLayout(Fl_Widget* w) {
    Fl_Image_Surface surf(w->w(), w->h());
    SurfaceScope here(&surf);
    w->draw();
    Fl_RGB_Image* img = surf.image();
    delete img;
}

inline int report(const char* suite) {
    printf("\n%s checks=%d failures=%d\n", suite, checks, failures);
    return failures ? 1 : 0;
}

// Renders an image and compares the pixels. Which picture a widget is drawing
// is a question about pixels, not about pointers: AssetLoader::load() returns
// a fresh copy on every call, so the object a widget holds is never equal to
// anything the suite can load itself, and never equal to the compiled-in glyph.
inline bool samePixels(Fl_Image* a, Fl_Image* b) {
    if (!a || !b || a->w() != b->w() || a->h() != b->h()) return false;
    const int w = a->w(), h = a->h();
    Fl_Image_Surface surf(w, h);
    std::vector<unsigned char> pa, pb;
    for (int pass = 0; pass < 2; pass++) {
        Fl_RGB_Image* out = nullptr;
        {
            SurfaceScope here(&surf);
            fl_color(FL_BLACK); // same backdrop for both
            fl_rectf(0, 0, w, h);
            (pass == 0 ? a : b)->draw(0, 0);
            out = surf.image();
        }
        if (!out || out->d() != 3 || out->w() != w || out->h() != h) {
            if (out) delete out;
            return false;
        }
        (pass == 0 ? pa : pb)
            .assign(out->array, out->array + (size_t)w * h * 3);
        delete out;
    }
    return pa == pb;
}

struct Rgb {
    unsigned char r, g, b;
};

// Renders the widget offscreen and reports the background it fills itself with
// plus the extent of everything else it draws (the icon). Rendered pixels, not
// the colour attributes, because those are what the user actually sees.
struct Rendered {
    Rgb bg;
    int x0, y0, x1, y1, ink;
    bool ok;
};

inline Rendered renderWidget(Fl_Widget* w) {
    Rendered r{};
    r.ok = false;
    r.x0 = r.y0 = r.x1 = r.y1 = -1;
    r.ink = 0;
    Fl_Image_Surface surf(w->w(), w->h());
    SurfaceScope here(&surf);
    surf.draw(w);
    Fl_RGB_Image* img = surf.image();
    if (!img || !img->array || img->d() != 3 || img->w() != w->w() ||
        img->h() != w->h()) {
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
            if (std::abs(p.r - r.bg.r) <= 6 && std::abs(p.g - r.bg.g) <= 6 &&
                std::abs(p.b - r.bg.b) <= 6)
                continue;
            r.ink++;
            if (r.x0 < 0 || x < r.x0) r.x0 = x;
            if (x > maxx) maxx = x;
            if (r.y0 < 0 || y < r.y0) r.y0 = y;
            if (y > maxy) maxy = y;
        }
    if (r.x0 >= 0) {
        r.x1 = maxx;
        r.y1 = maxy;
    }
    r.ok = true;
    delete img;
    return r;
}

// Extent of what a paint call actually covers on a black backdrop, in pixels.
// The badge group is judged on this rather than on the decoded image size: a
// PNG whose art fills its box and the shield inside the same box do not paint
// the same area, and the difference is what the eye reads as "bigger".
struct InkBox {
    int w = 0, h = 0;
    bool ok = false;
};

inline InkBox inkOfPaint(int side, const std::function<void()>& paint) {
    InkBox box;
    Fl_Image_Surface surf(side, side);
    Fl_RGB_Image* img = nullptr;
    {
        SurfaceScope here(&surf);
        fl_color(FL_BLACK);
        fl_rectf(0, 0, side, side);
        paint();
        img = surf.image();
    }
    if (!img || !img->array || img->d() != 3) {
        if (img) delete img;
        return box;
    }
    const unsigned char* px = img->array;
    int x0 = side, y0 = side, x1 = -1, y1 = -1;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const unsigned char* q = px + (y * side + x) * 3;
            if (!q[0] && !q[1] && !q[2]) continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }
    delete img;
    if (x1 < 0) return box;
    box.w = x1 - x0 + 1;
    box.h = y1 - y0 + 1;
    box.ok = true;
    return box;
}

inline InkBox inkOfImage(int w, int h, Fl_Image* img) {
    return inkOfPaint((w > h ? w : h) + 8, [img] { img->draw(0, 0); });
}

} // namespace testsupport

#endif // FTORRENT_TESTS_TEST_SUPPORT_H