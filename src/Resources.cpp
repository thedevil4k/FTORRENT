#include "Resources.h"
#include <FL/Fl_PNG_Image.H>

// Static member initialization
Fl_RGB_Image* Resources::s_logo = nullptr;
Fl_Pixmap* Resources::s_iconAdd = nullptr;
Fl_Pixmap* Resources::s_iconPause = nullptr;
Fl_Pixmap* Resources::s_iconPlay = nullptr;
Fl_Pixmap* Resources::s_iconRemove = nullptr;
Fl_Pixmap* Resources::s_iconSettings = nullptr;
Fl_Pixmap* Resources::s_iconDownload = nullptr;
Fl_Pixmap* Resources::s_iconUpload = nullptr;

void Resources::initialize() {
    // Create logo from PNG memory buffer
    s_logo = new Fl_PNG_Image("logo.png", ftorrent_logo_png, ftorrent_logo_png_size);
    s_iconAdd = new Fl_Pixmap(icon_add_xpm);
    s_iconPause = new Fl_Pixmap(icon_pause_xpm);
    s_iconPlay = new Fl_Pixmap(icon_play_xpm);
    s_iconRemove = new Fl_Pixmap(icon_remove_xpm);
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
    
    s_logo = nullptr;
    s_iconAdd = nullptr;
    s_iconPause = nullptr;
    s_iconPlay = nullptr;
    s_iconRemove = nullptr;
    s_iconSettings = nullptr;
    s_iconDownload = nullptr;
    s_iconUpload = nullptr;
}

Fl_RGB_Image* Resources::getLogoImage() {
    return s_logo;
}

Fl_Pixmap* Resources::getAddIcon() {
    return s_iconAdd;
}

Fl_Pixmap* Resources::getPauseIcon() {
    return s_iconPause;
}

Fl_Pixmap* Resources::getPlayIcon() {
    return s_iconPlay;
}

Fl_Pixmap* Resources::getRemoveIcon() {
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
