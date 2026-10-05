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

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Widget.H>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

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
//   Windows  SHGetFolderPathA(CSIDL_APPDATA), which follows %APPDATA%
//   macOS    $HOME/Library/Application Support/FTorrent -- and NOTHING else, so
//            XDG_CONFIG_HOME is ignored there
//   Linux    $XDG_CONFIG_HOME, falling back to $HOME/.config
inline std::filesystem::path configDir() {
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    return std::filesystem::path(appdata ? appdata : ".") / "FTorrent";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return std::filesystem::path(home ? home : ".") /
           "Library" / "Application Support" / "FTorrent";
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
    std::error_code ec2;
    std::filesystem::create_directories(configDir(), ec2);
}


// Remove the scratch directory, so a test run leaves nothing behind.
inline void removeScratchConfig(const char* name) {
    std::error_code ec;
    std::filesystem::remove_all(scratchConfigDir(name), ec);
}

// Let a container place its children the way the window does, by drawing it.
inline void forceLayout(Fl_Widget* w) {
    Fl_Image_Surface surf(w->w(), w->h());
    Fl_Surface_Device::push_current(&surf);
    w->draw();
    Fl_RGB_Image* img = surf.image();
    Fl_Surface_Device::pop_current();
    delete img;
}

inline int report(const char* suite) {
    printf("\n%s checks=%d failures=%d\n", suite, checks, failures);
    return failures ? 1 : 0;
}

}  // namespace testsupport

#endif  // FTORRENT_TESTS_TEST_SUPPORT_H