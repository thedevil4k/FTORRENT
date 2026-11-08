#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Secret_Input.H>

#include "SecurityKeys.h"

/**
 * @brief Preferences window of FTorrent.
 *
 * Organised in tabs:
 * - General: paths and behaviour
 * - Connection: port, speed and connection limits
 * - BitTorrent: DHT, PEX, LSD, UPnP, NAT-PMP
 * - Advanced: the user agent and anonymous mode
 * - Security: the provider keys, stored by the OS and never in settings.ini
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
    Fl_Check_Button* m_showPublicIp;

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

    // Security tab widgets. The key is stored through SecurityKeys (DPAPI on
    // Windows, a 0600 file on Linux), never in settings.ini.
    Fl_Group* m_securityTab;
    Fl_Secret_Input* m_virusTotalKey;
    Fl_Button* m_getVirusTotalKey;
    Fl_Button* m_saveKey;
    Fl_Button* m_clearKey;
    Fl_Secret_Input* m_malwareBazaarKey;
    Fl_Button* m_getBazaarKey;
    Fl_Button* m_saveBazaarKey;
    Fl_Button* m_clearBazaarKey;
    Fl_Box* m_keyStatus;

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
    void createSecurityTab();
    void createButtons();

    // Security tab
    void loadKeyIntoField();
    void refreshKeyStatus();
    // Writes one field when it holds a non-empty key. Returns false only when
    // the write failed; an empty field is a no-op.
    bool storeKey(SecurityKeys::Provider provider, Fl_Input* field);
    void saveKeyFromField(SecurityKeys::Provider provider, Fl_Input* field);
    // Stores every non-empty key field. Used by OK/Apply so pasting a key and
    // accepting the dialog is enough; an empty field never removes a key,
    // because "Remove key" is the explicit way to do that.
    bool persistKeyFields();
    void clearKeyFor(SecurityKeys::Provider provider, Fl_Input* field);
    static void onSaveKey(Fl_Widget* w, void* data);
    static void onClearKey(Fl_Widget* w, void* data);
    static void onSaveBazaarKey(Fl_Widget* w, void* data);
    static void onClearBazaarKey(Fl_Widget* w, void* data);
    static void onGetVirusTotalKey(Fl_Widget* w, void* data);
    static void onGetBazaarKey(Fl_Widget* w, void* data);

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
