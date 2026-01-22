#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <string>
#include <vector>
#include <FL/Fl_File_Chooser.H>

class PathUtils {
public:
    static std::string getHomePath();
    static std::string getDesktopPath();
    static std::string getDownloadsPath();
    
    static const char* showFileChooser(const char* message, const char* filter, const char* filename, int type = Fl_File_Chooser::CREATE);
    static const char* showDirChooser(const char* message, const char* path);
    static std::string getAppDirPath();
    
    /**
     * @brief Initializes FLTK file chooser favorites with system paths like Desktop and Downloads
     */
    static void initFileChooserFavorites();
};

#endif // PATHUTILS_H
