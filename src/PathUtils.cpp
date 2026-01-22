#include "PathUtils.h"
#include <FL/Fl_File_Chooser.H>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif
#include <FL/Fl.H>
#include <filesystem>

std::string PathUtils::getHomePath() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return std::string(path);
    }
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) return std::string(userProfile);
    return "C:\\";
#else
    const char* home = getenv("HOME");
    if (home) return std::string(home);
    struct passwd* pw = getpwuid(getuid());
    if (pw) return std::string(pw->pw_dir);
    return "/";
#endif
}

std::string PathUtils::getDesktopPath() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, path))) {
        return std::string(path);
    }
    return getHomePath() + "\\Desktop";
#else
    // On Linux, desktop might be localized, but ~/Desktop is standard enough
    // Ideally we should check XDG_USER_DIRS, but this is a good fallback
    return getHomePath() + "/Desktop";
#endif
}

std::string PathUtils::getDownloadsPath() {
#ifdef _WIN32
    char path[MAX_PATH];
    // CSIDL_DOWNLOADS is not available in older Windows, but SHGetKnownFolderPath is better for Vista+
    // But for simplicity with CSIDL:
    // There isn't a CSIDL for Downloads in old versions. 
    // We can try to get it from the registry or just append \Downloads to profile.
    // In Windows 7+ it's a known folder.
    
    // Attempting to use SHGetKnownFolderPath if possible, but for now let's stick to CSIDL style or simple append
    return getHomePath() + "\\Downloads";
#else
    return getHomePath() + "/Downloads";
#endif
}

const char* PathUtils::showFileChooser(const char* message, const char* filter, const char* filename, int type) {
    static Fl_File_Chooser* chooser = nullptr;
    if (!chooser) {
        chooser = new Fl_File_Chooser(".", filter, type, message);
        
        // Add default favorites
        std::string desktop = getDesktopPath();
        std::string downloads = getDownloadsPath();
        
        // Replace backslashes with forward slashes for FLTK on Windows (it prefers them)
        std::string d_desktop = desktop;
        std::replace(d_desktop.begin(), d_desktop.end(), '\\', '/');
        std::string d_downloads = downloads;
        std::replace(d_downloads.begin(), d_downloads.end(), '\\', '/');

        // Note: add_favorite is only available in FLTK 1.4+. 
        // For 1.3, we can't easily add favorites programmatically to a specific instance 
        // without modifying the global preferences.
    } else {
        chooser->label(message);
        chooser->filter(filter);
        chooser->type(type);
    }
    
    if (filename) chooser->value(filename);
    
    chooser->show();
    while (chooser->shown()) {
        Fl::wait();
    }
    
    if (chooser->value()) return chooser->value();
    return nullptr;
}

const char* PathUtils::showDirChooser(const char* message, const char* path) {
    static Fl_File_Chooser* chooser = nullptr;
    if (!chooser) {
        chooser = new Fl_File_Chooser(".", "*", Fl_File_Chooser::DIRECTORY, message);
        
        // Add default favorites
        std::string desktop = getDesktopPath();
        std::string downloads = getDownloadsPath();
        
        std::string d_desktop = desktop;
        std::replace(d_desktop.begin(), d_desktop.end(), '\\', '/');
        std::string d_downloads = downloads;
        std::replace(d_downloads.begin(), d_downloads.end(), '\\', '/');

        // Note: add_favorite is only available in FLTK 1.4+.
    } else {
        chooser->label(message);
        chooser->filter("*");
        chooser->type(Fl_File_Chooser::DIRECTORY);
    }
    
    if (path) chooser->value(path);
    
    chooser->show();
    while (chooser->shown()) {
        Fl::wait();
    }
    
    if (chooser->value()) return chooser->value();
    return nullptr;
}

void PathUtils::initFileChooserFavorites() {
    // This is now handled inside showFileChooser and showDirChooser
}

std::string PathUtils::getAppDirPath() {
    try {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        std::filesystem::path p(path);
        std::filesystem::path parent = p.parent_path();
        std::u8string u8 = parent.u8string();
        return std::string((const char*)u8.data(), u8.size());
#elif defined(__APPLE__)
        // macOS path handling could be here, but for now fallback to standard nix
        return ".";
#else
        std::filesystem::path p = std::filesystem::canonical("/proc/self/exe");
        std::filesystem::path parent = p.parent_path();
        std::u8string u8 = parent.u8string();
        return std::string((const char*)u8.data(), u8.size());
#endif
    } catch (...) {
        return ".";
    }
}
