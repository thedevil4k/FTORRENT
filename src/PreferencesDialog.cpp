#include "PreferencesDialog.h"
#include "SettingsManager.h"
#include "SecurityKeys.h"
#include "SystemUtils.h"
#include "Version.h"
#include <FL/Fl.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include "PathUtils.h"
#include <cstdlib>
#include <ctime>
#include <sstream>

PreferencesDialog::PreferencesDialog()
    : Fl_Window(600, 450, "Preferences")
    , m_tabs(nullptr)
    , m_okClicked(false) {
    // Create tabs
    m_tabs = new Fl_Tabs(10, 10, 580, 380);

    createGeneralTab();
    createConnectionTab();
    createBitTorrentTab();
    createAdvancedTab();
    createSecurityTab();

    m_tabs->end();

    // Create buttons
    createButtons();

    end();
    set_modal();

    // Load current settings
    loadSettings();
}

PreferencesDialog::~PreferencesDialog() {}

void PreferencesDialog::createGeneralTab() {
    m_generalTab = new Fl_Group(10, 35, 580, 355, "General");
    m_generalTab->begin();

    // Download path
    Fl_Box* pathLabel = new Fl_Box(20, 50, 150, 25, "Download Path:");
    pathLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_downloadPath = new Fl_Input(20, 75, 450, 30);

    m_browsePath = new Fl_Button(480, 75, 100, 30, "Browse...");
    m_browsePath->callback(onBrowsePath, this);

    // Start with system
    m_startWithSystem = new Fl_Check_Button(20, 120, 250, 25,
                                            "Start FTorrent on system startup");

    // Minimize to tray
    m_minimizeToTray =
        new Fl_Check_Button(20, 150, 250, 25, "Minimize to system tray");

    // Show public IP in the status bar (privacy)
    m_showPublicIp =
        new Fl_Check_Button(20, 180, 300, 25, "Show public IP in status bar");
    m_showPublicIp->tooltip(
        "When off, the public IP is not displayed and is not looked up "
        "online.\n"
        "The eye button in the status bar censors the IP while this is on.");

    m_generalTab->end();
}

void PreferencesDialog::createConnectionTab() {
    m_connectionTab = new Fl_Group(10, 35, 580, 355, "Connection");
    m_connectionTab->hide();
    m_connectionTab->begin();

    // Listen port
    Fl_Box* portLabel = new Fl_Box(20, 50, 150, 25, "Listening Port:");
    portLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_listenPort = new Fl_Int_Input(180, 50, 100, 25);
    m_listenPort->value("6881");

    m_randomPort =
        new Fl_Check_Button(290, 50, 200, 25, "Use random port on startup");
    m_randomPort->callback(onRandomPort, this);

    // Max download rate
    Fl_Box* downLabel = new Fl_Box(20, 90, 150, 25, "Max Download Rate:");
    downLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_maxDownloadRate = new Fl_Int_Input(180, 90, 100, 25);
    m_maxDownloadRate->value("0");

    Fl_Box* downUnit = new Fl_Box(290, 90, 100, 25, "KB/s (0 = unlimited)");
    downUnit->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    // Max upload rate
    Fl_Box* upLabel = new Fl_Box(20, 125, 150, 25, "Max Upload Rate:");
    upLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_maxUploadRate = new Fl_Int_Input(180, 125, 100, 25);
    m_maxUploadRate->value("0");

    Fl_Box* upUnit = new Fl_Box(290, 125, 100, 25, "KB/s (0 = unlimited)");
    upUnit->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    // Max connections
    Fl_Box* connLabel = new Fl_Box(20, 160, 150, 25, "Max Connections:");
    connLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_maxConnections = new Fl_Int_Input(180, 160, 100, 25);
    m_maxConnections->value("200");

    // Limit local network
    m_limitLocalNetwork = new Fl_Check_Button(
        20, 200, 300, 25, "Apply rate limits to local network");

    m_connectionTab->end();
}

void PreferencesDialog::createBitTorrentTab() {
    m_bittorrentTab = new Fl_Group(10, 35, 580, 355, "BitTorrent");
    m_bittorrentTab->hide();
    m_bittorrentTab->begin();

    Fl_Box* headerLabel =
        new Fl_Box(20, 50, 400, 25, "Enable/Disable Protocol Features:");
    headerLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    headerLabel->labelfont(FL_BOLD);

    m_enableDHT = new Fl_Check_Button(20, 85, 250, 25,
                                      "Enable DHT (Distributed Hash Table)");
    m_enablePEX =
        new Fl_Check_Button(20, 115, 250, 25, "Enable PEX (Peer Exchange)");
    m_enableLSD = new Fl_Check_Button(20, 145, 250, 25,
                                      "Enable LSD (Local Service Discovery)");
    m_enableUPnP =
        new Fl_Check_Button(20, 175, 250, 25, "Enable UPnP (Port Forwarding)");
    m_enableNATPMP = new Fl_Check_Button(20, 205, 250, 25, "Enable NAT-PMP");

    // Info texts
    Fl_Box* dhtInfo =
        new Fl_Box(280, 85, 290, 25, "Find peers without trackers");
    dhtInfo->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    dhtInfo->labelsize(11);
    dhtInfo->labelcolor(fl_rgb_color(100, 100, 100));

    Fl_Box* pexInfo =
        new Fl_Box(280, 115, 290, 25, "Exchange peers with other clients");
    pexInfo->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    pexInfo->labelsize(11);
    pexInfo->labelcolor(fl_rgb_color(100, 100, 100));

    Fl_Box* lsdInfo =
        new Fl_Box(280, 145, 290, 25, "Find peers on local network");
    lsdInfo->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lsdInfo->labelsize(11);
    lsdInfo->labelcolor(fl_rgb_color(100, 100, 100));

    m_bittorrentTab->end();
}

void PreferencesDialog::createAdvancedTab() {
    m_advancedTab = new Fl_Group(10, 35, 580, 355, "Advanced");
    m_advancedTab->hide();
    m_advancedTab->begin();

    // User agent
    Fl_Box* uaLabel = new Fl_Box(20, 50, 150, 25, "User Agent:");
    uaLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_userAgent = new Fl_Input(20, 75, 400, 30);
    m_userAgent->value(Version::defaultTrackerUserAgent().c_str());

    // Anonymous mode
    m_anonymousMode = new Fl_Check_Button(20, 120, 300, 25,
                                          "Anonymous mode (disable tracking)");

    Fl_Box* warning = new Fl_Box(
        20, 160, 550, 60,
        "Warning: Changing advanced settings may affect performance.\n"
        "Only modify these if you know what you're doing.");
    warning->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    warning->labelcolor(fl_rgb_color(200, 100, 0));

    m_advancedTab->end();
}

void PreferencesDialog::createSecurityTab() {
    m_securityTab = new Fl_Group(10, 35, 580, 355, "Security");
    m_securityTab->hide();
    m_securityTab->begin();

    Fl_Box* keyLabel = new Fl_Box(20, 44, 200, 22, "VirusTotal API key:");
    keyLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_virusTotalKey = new Fl_Secret_Input(20, 66, 400, 28);
    m_virusTotalKey->tooltip(
        "A free VirusTotal API key. Used only when you click a shield.");

    // One click takes the user to the page that issues the key: the point of
    // this button is that nobody has to search for it. The key itself is still
    // the user's own and is never shipped with the program.
    m_getVirusTotalKey = new Fl_Button(430, 66, 130, 28, "Get free key");
    m_getVirusTotalKey->callback(onGetVirusTotalKey, this);
    m_getVirusTotalKey->tooltip(
        "Opens virustotal.com to create a free account and copy your own API "
        "key. FTorrent never ships or shares a key.");

    m_saveKey = new Fl_Button(20, 100, 120, 26, "Save key");
    m_saveKey->callback(onSaveKey, this);

    m_clearKey = new Fl_Button(150, 100, 120, 26, "Remove key");
    m_clearKey->callback(onClearKey, this);

    Fl_Box* bazaarLabel =
        new Fl_Box(20, 136, 260, 22, "MalwareBazaar Auth-Key:");
    bazaarLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_malwareBazaarKey = new Fl_Secret_Input(20, 158, 400, 28);
    m_malwareBazaarKey->tooltip(
        "The abuse.ch Auth-Key. MalwareBazaar answers HTTP 401 without one.");

    m_getBazaarKey = new Fl_Button(430, 158, 130, 28, "Get free key");
    m_getBazaarKey->callback(onGetBazaarKey, this);
    m_getBazaarKey->tooltip(
        "Opens auth.abuse.ch to create the free Auth-Key MalwareBazaar asks "
        "for. FTorrent never ships or shares a key.");

    m_saveBazaarKey = new Fl_Button(20, 192, 120, 26, "Save key");
    m_saveBazaarKey->callback(onSaveBazaarKey, this);

    m_clearBazaarKey = new Fl_Button(150, 192, 120, 26, "Remove key");
    m_clearBazaarKey->callback(onClearBazaarKey, this);

    m_keyStatus = new Fl_Box(20, 224, 550, 44);
    m_keyStatus->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_keyStatus->labelsize(11);

    Fl_Box* note = new Fl_Box(20, 272, 550, 78);
    note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    note->labelsize(11);
    const std::string text =
        "Reputation lookups send only the SHA-256 of a file, and only after "
        "you "
        "click a shield. Nothing is uploaded and nothing is looked up on its "
        "own. Both providers need a key; without one it is skipped and the "
        "other is still asked.\n\n" +
        SecurityKeys::storageDescription();
    note->copy_label(text.c_str());

    m_securityTab->end();
}

void PreferencesDialog::loadKeyIntoField() {
    const auto fill = [](Fl_Input* field, SecurityKeys::Provider provider) {
        if (!field) return;
        std::string key;
        if (SecurityKeys::load(provider, key)) field->value(key.c_str());
        else field->value("");
    };
    fill(m_virusTotalKey, SecurityKeys::Provider::VirusTotal);
    fill(m_malwareBazaarKey, SecurityKeys::Provider::MalwareBazaar);
}

void PreferencesDialog::refreshKeyStatus() {
    if (!m_keyStatus) return;
    const bool vt = SecurityKeys::exists(SecurityKeys::Provider::VirusTotal);
    const bool bazaar =
        SecurityKeys::exists(SecurityKeys::Provider::MalwareBazaar);

    // The state of each provider first, so the answer to "what is missing?"
    // does not depend on reading a paragraph.
    std::string text = std::string("VirusTotal: ") +
                       (vt ? "key saved" : "no key") + "   |   " +
                       "MalwareBazaar: " + (bazaar ? "key saved" : "no key") +
                       "\n";
    if (vt && bazaar) {
        text += "A shield check asks both providers.";
    } else if (vt) {
        text += "MalwareBazaar is skipped until its Auth-Key is added.";
    } else if (bazaar) {
        text += "VirusTotal is skipped until its API key is added.";
    } else {
        text += "A shield check cannot ask either provider until one key is "
                "added; Get free key opens the page that issues it.";
    }
    m_keyStatus->copy_label(text.c_str());
    m_keyStatus->labelcolor(vt && bazaar ? FL_FOREGROUND_COLOR
                                         : fl_rgb_color(150, 110, 0));
}

void PreferencesDialog::saveKeyFromField(SecurityKeys::Provider provider,
                                         Fl_Input* field) {
    if (!field) return;

    // A key pasted from a web page or an email often carries spaces, tabs or a
    // line break in the middle. None of them are part of any key, so every one
    // is dropped instead of asking the user to clean it up.
    const std::string key =
        SecurityKeys::normalize(field->value() ? field->value() : "");

    if (key.empty()) {
        fl_alert("Paste the key first, or use Remove key.");
        return;
    }

    if (storeKey(provider, field)) refreshKeyStatus();
}

bool PreferencesDialog::storeKey(SecurityKeys::Provider provider,
                                 Fl_Input* field) {
    if (!field) return true;

    const std::string key =
        SecurityKeys::normalize(field->value() ? field->value() : "");
    if (key.empty()) return true;

    std::string error;
    if (!SecurityKeys::save(provider, key, error)) {
        fl_alert("The key could not be saved: %s", error.c_str());
        return false;
    }
    // Show what was actually stored, without the paste noise.
    field->value(key.c_str());
    return true;
}

bool PreferencesDialog::persistKeyFields() {
    // Both fields are attempted, so a failure on one does not silently skip
    // the other. An empty field is a no-op: "Remove key" is the only action
    // that deletes one.
    const bool virusTotal =
        storeKey(SecurityKeys::Provider::VirusTotal, m_virusTotalKey);
    const bool malwareBazaar =
        storeKey(SecurityKeys::Provider::MalwareBazaar, m_malwareBazaarKey);
    refreshKeyStatus();
    return virusTotal && malwareBazaar;
}

void PreferencesDialog::clearKeyFor(SecurityKeys::Provider provider,
                                    Fl_Input* field) {
    SecurityKeys::remove(provider);
    if (field) field->value("");
    refreshKeyStatus();
}

void PreferencesDialog::onSaveKey(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = static_cast<PreferencesDialog*>(data);
    if (!dlg) return;
    dlg->saveKeyFromField(SecurityKeys::Provider::VirusTotal,
                          dlg->m_virusTotalKey);
}

void PreferencesDialog::onClearKey(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = static_cast<PreferencesDialog*>(data);
    if (!dlg) return;
    dlg->clearKeyFor(SecurityKeys::Provider::VirusTotal, dlg->m_virusTotalKey);
}

void PreferencesDialog::onSaveBazaarKey(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = static_cast<PreferencesDialog*>(data);
    if (!dlg) return;
    dlg->saveKeyFromField(SecurityKeys::Provider::MalwareBazaar,
                          dlg->m_malwareBazaarKey);
}

void PreferencesDialog::onClearBazaarKey(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = static_cast<PreferencesDialog*>(data);
    if (!dlg) return;
    dlg->clearKeyFor(SecurityKeys::Provider::MalwareBazaar,
                     dlg->m_malwareBazaarKey);
}

// The key pages, not the home page: the user lands where the key is issued.
void PreferencesDialog::onGetVirusTotalKey(Fl_Widget*, void*) {
    SystemUtils::openUrl("https://www.virustotal.com/gui/my-apikey");
}

void PreferencesDialog::onGetBazaarKey(Fl_Widget*, void*) {
    SystemUtils::openUrl("https://auth.abuse.ch/");
}

void PreferencesDialog::createButtons() {
    int btn_y = 400;
    int btn_w = 80;
    int btn_h = 30;

    m_okButton = new Fl_Button(320, btn_y, btn_w, btn_h, "OK");
    m_okButton->callback(onOK, this);

    m_cancelButton = new Fl_Button(410, btn_y, btn_w, btn_h, "Cancel");
    m_cancelButton->callback(onCancel, this);

    m_applyButton = new Fl_Button(500, btn_y, btn_w, btn_h, "Apply");
    m_applyButton->callback(onApply, this);
}

void PreferencesDialog::loadSettings() {
    auto& settings = SettingsManager::instance();
    settings.load();

    // General
    m_downloadPath->value(settings.getDefaultSavePath().c_str());
    m_startWithSystem->value(settings.getStartWithSystem() ? 1 : 0);
    m_minimizeToTray->value(settings.getMinimizeToTray() ? 1 : 0);
    m_showPublicIp->value(settings.getShowPublicIp() ? 1 : 0);

    // Connection
    std::ostringstream port, maxDown, maxUp, maxConn;
    port << settings.getListenPort();
    maxDown << settings.getMaxDownloadRate();
    maxUp << settings.getMaxUploadRate();
    maxConn << settings.getMaxConnections();

    m_listenPort->value(port.str().c_str());
    m_maxDownloadRate->value(maxDown.str().c_str());
    m_maxUploadRate->value(maxUp.str().c_str());
    m_maxConnections->value(maxConn.str().c_str());

    // BitTorrent
    m_enableDHT->value(settings.getDHTEnabled() ? 1 : 0);
    m_enablePEX->value(settings.getPEXEnabled() ? 1 : 0);
    m_enableLSD->value(settings.getLSDEnabled() ? 1 : 0);
    m_enableUPnP->value(settings.getUPnPEnabled() ? 1 : 0);

    // Advanced
    m_userAgent->value(settings.getUserAgent().c_str());

    // Security
    loadKeyIntoField();
    refreshKeyStatus();
}

void PreferencesDialog::saveSettings() {
    auto& settings = SettingsManager::instance();

    // General
    settings.setDefaultSavePath(m_downloadPath->value());
    settings.setStartWithSystem(m_startWithSystem->value() != 0);
    settings.setMinimizeToTray(m_minimizeToTray->value() != 0);
    settings.setShowPublicIp(m_showPublicIp->value() != 0);

    // Connection
    settings.setListenPort(atoi(m_listenPort->value()));
    settings.setMaxDownloadRate(atoi(m_maxDownloadRate->value()));
    settings.setMaxUploadRate(atoi(m_maxUploadRate->value()));
    settings.setMaxConnections(atoi(m_maxConnections->value()));

    // BitTorrent
    settings.setDHTEnabled(m_enableDHT->value() != 0);
    settings.setPEXEnabled(m_enablePEX->value() != 0);
    settings.setLSDEnabled(m_enableLSD->value() != 0);
    settings.setUPnPEnabled(m_enableUPnP->value() != 0);

    // Advanced
    settings.setUserAgent(m_userAgent->value());

    settings.save();
}

void PreferencesDialog::applySettings() {
    if (validateSettings() && persistKeyFields()) {
        saveSettings();
        fl_message("Settings applied successfully.\nRestart may be required "
                   "for some changes.");
    }
}

bool PreferencesDialog::validateSettings() {
    // Validate port
    int port = atoi(m_listenPort->value());
    if (port < 1024 || port > 65535) {
        fl_alert("Port must be between 1024 and 65535");
        m_tabs->value(m_connectionTab);
        return false;
    }

    // Validate download path
    if (strlen(m_downloadPath->value()) == 0) {
        fl_alert("Download path cannot be empty");
        m_tabs->value(m_generalTab);
        return false;
    }

    // Validate connection limits
    int maxConn = atoi(m_maxConnections->value());
    if (maxConn < 2 || maxConn > 10000) {
        fl_alert("Max connections must be between 2 and 10000");
        m_tabs->value(m_connectionTab);
        return false;
    }

    return true;
}

bool PreferencesDialog::show_modal() {
    m_okClicked = false;
    show();

    while (shown()) {
        Fl::wait();
    }

    return m_okClicked;
}

void PreferencesDialog::generateRandomPort() {
    srand(time(nullptr));
    int port = 49152 + (rand() % (65535 - 49152 + 1));

    std::ostringstream oss;
    oss << port;
    m_listenPort->value(oss.str().c_str());
}

// Static callbacks
void PreferencesDialog::onBrowsePath(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = (PreferencesDialog*)data;

    const char* dir = PathUtils::showDirChooser(
        "Select Default Download Directory", dlg->m_downloadPath->value());
    if (dir) {
        dlg->m_downloadPath->value(dir);
    }
}

void PreferencesDialog::onRandomPort(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = (PreferencesDialog*)data;

    if (dlg->m_randomPort->value()) {
        dlg->generateRandomPort();
        dlg->m_listenPort->deactivate();
    } else {
        dlg->m_listenPort->activate();
    }
}

void PreferencesDialog::onOK(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = (PreferencesDialog*)data;

    if (dlg->validateSettings() && dlg->persistKeyFields()) {
        dlg->saveSettings();
        dlg->m_okClicked = true;
        dlg->hide();
    }
}

void PreferencesDialog::onCancel(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = (PreferencesDialog*)data;
    dlg->m_okClicked = false;
    dlg->hide();
}

void PreferencesDialog::onApply(Fl_Widget*, void* data) {
    PreferencesDialog* dlg = (PreferencesDialog*)data;
    dlg->applySettings();
}
