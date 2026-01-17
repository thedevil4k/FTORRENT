#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>

/**
 * @brief Diálogo de preferencias/configuración de FLTorrent
 * 
 * Organizado en pestañas:
 * - General: Rutas, comportamiento
 * - Connection: Puertos, límites de velocidad
 * - BitTorrent: DHT, PEX, LSD, UPnP
 * - Advanced: Configuraciones avanzadas
 */
class PreferencesDialog : public Fl_Window {
public:
    PreferencesDialog();
    ~PreferencesDialog();
    
    // Show dialog and return true if OK was clicked
    bool show_modal();
    
private:
    // Tabs
    Fl_Tabs* m_tabs;
    
    // General tab widgets
    Fl_Group* m_generalTab;
    Fl_Input* m_downloadPath;
    Fl_Button* m_browsePath;
    Fl_Check_Button* m_startWithSystem;
    Fl_Check_Button* m_minimizeToTray;
    
    // Connection tab widgets
    Fl_Group* m_connectionTab;
    Fl_Int_Input* m_listenPort;
    Fl_Check_Button* m_randomPort;
    Fl_Int_Input* m_maxDownloadRate;
    Fl_Int_Input* m_maxUploadRate;
    Fl_Int_Input* m_maxConnections;
    Fl_Check_Button* m_limitLocalNetwork;
    
    // BitTorrent tab widgets
    Fl_Group* m_bittorrentTab;
    Fl_Check_Button* m_enableDHT;
    Fl_Check_Button* m_enablePEX;
    Fl_Check_Button* m_enableLSD;
    Fl_Check_Button* m_enableUPnP;
    Fl_Check_Button* m_enableNATPMP;
    
    // Advanced tab widgets
    Fl_Group* m_advancedTab;
    Fl_Input* m_userAgent;
    Fl_Check_Button* m_anonymousMode;
    
    // Buttons
    Fl_Button* m_okButton;
    Fl_Button* m_cancelButton;
    Fl_Button* m_applyButton;
    
    // State
    bool m_okClicked;
    
    // UI Creation
    void createGeneralTab();
    void createConnectionTab();
    void createBitTorrentTab();
    void createAdvancedTab();
    void createButtons();
    
    // Load/Save
    void loadSettings();
    void saveSettings();
    void applySettings();
    
    // Validation
    bool validateSettings();
    
    // Callbacks
    static void onBrowsePath(Fl_Widget* w, void* data);
    static void onRandomPort(Fl_Widget* w, void* data);
    static void onOK(Fl_Widget* w, void* data);
    static void onCancel(Fl_Widget* w, void* data);
    static void onApply(Fl_Widget* w, void* data);
    
    // Helper
    void generateRandomPort();
};

#endif // PREFERENCESDIALOG_H
