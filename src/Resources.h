#ifndef RESOURCES_H
#define RESOURCES_H

#include <FL/Fl_Pixmap.H>
#include <FL/Fl_RGB_Image.H>
#include "Icons.h"
#include "LogoIcon.h"

/**
 * @brief Resource manager for FLTorrent
 * 
 * Provides easy access to application icons and resources.
 * Uses XPM format for embedded icons.
 */
class Resources {
public:
    // Initialize all resources (call once at startup)
    static void initialize();
    
    // Cleanup resources
    static void cleanup();
    
    // Get icons
    static Fl_RGB_Image* getLogoImage();
    static Fl_Pixmap* getAddIcon();
    static Fl_Pixmap* getPauseIcon();
    static Fl_Pixmap* getPlayIcon();
    static Fl_Pixmap* getRemoveIcon();
    static Fl_Pixmap* getSettingsIcon();
    static Fl_Pixmap* getDownloadIcon();
    static Fl_Pixmap* getUploadIcon();
    
private:
    static Fl_RGB_Image* s_logo;
    static Fl_Pixmap* s_iconAdd;
    static Fl_Pixmap* s_iconPause;
    static Fl_Pixmap* s_iconPlay;
    static Fl_Pixmap* s_iconRemove;
    static Fl_Pixmap* s_iconSettings;
    static Fl_Pixmap* s_iconDownload;
    static Fl_Pixmap* s_iconUpload;
};

#endif // RESOURCES_H
