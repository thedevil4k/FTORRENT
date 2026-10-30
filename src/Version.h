#ifndef FTORRENT_VERSION_H
#define FTORRENT_VERSION_H

#include <string>

// The version is defined once, by the build: CMakeLists.txt turns
// `project(FTorrent VERSION ...)` into the APP_VERSION_STRING definition below.
// Every user agent and every package name derives from it, so a release is one
// number in one file rather than four strings that drift apart.
#ifndef APP_VERSION_STRING
#  define APP_VERSION_STRING "0.0.0-unknown"
#endif

namespace Version {

inline const char* string() { return APP_VERSION_STRING; }

// "FTorrent/0.6.0": the HTTP user agent used for provider lookups and for
// fetching .torrent files.
inline std::string httpUserAgent() {
    return std::string("FTorrent/") + string();
}

// "FTORRENT 0.6.0": the BitTorrent user agent, which the user may replace in
// Preferences and which is what a tracker sees. It keeps its own spelling on
// purpose, so changing one never silently changes the other.
inline std::string defaultTrackerUserAgent() {
    return std::string("FTORRENT ") + string();
}

} // namespace Version

#endif // FTORRENT_VERSION_H
