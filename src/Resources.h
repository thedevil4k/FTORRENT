#ifndef RESOURCES_H
#define RESOURCES_H

#include <FL/Fl_Image.H>
#include <FL/Fl_Pixmap.H>
#include <FL/Fl_RGB_Image.H>
#include "Icons.h"
#include "LogoIcon.h"

/**
 * @brief Resource manager for FTorrent
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
    // The glyphs the toolbar shows at icon size come back as Fl_Image* rather
    // than Fl_Pixmap* because they are resampled at initialize(): see
    // rescaleGlyph() in the .cpp for why the raw 16x16 XPMs cannot simply be
    // scaled. Search is in this group even though it normally comes from the
    // PNGs, because it is the fallback the search button uses when those are
    // missing, and a fallback that draws smaller than its neighbours is worse
    // than no fallback at all.
    static Fl_Image* getPauseIcon();
    static Fl_Image* getPlayIcon();
    static Fl_Image* getRemoveIcon();
    static Fl_Pixmap* getSettingsIcon();
    static Fl_Pixmap* getDownloadIcon();
    static Fl_Pixmap* getUploadIcon();
    static Fl_Image* getSearchIcon();
    
private:
    static Fl_RGB_Image* s_logo;
    static Fl_Pixmap* s_iconAdd;
    static Fl_Image* s_iconPause;
    static Fl_Image* s_iconPlay;
    static Fl_Image* s_iconRemove;
    static Fl_Pixmap* s_iconSettings;
    static Fl_Pixmap* s_iconDownload;
    static Fl_Pixmap* s_iconUpload;
    static Fl_Image* s_iconSearch;
};

#endif // RESOURCES_H
