#include "SettingsManager.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
    m_settingsPath = getConfigPath();
    setDefaults();
}

SettingsManager::~SettingsManager() {
    save();
}

bool SettingsManager::load() {
    std::ifstream file(m_settingsPath);
    if (!file.is_open()) {
        // File doesn't exist, use defaults
        return save(); // Create default settings file
    }

    std::string line;
    while (std::getline(file, line)) {
        parseLine(line);
    }

    file.close();
    return true;
}

bool SettingsManager::save() {
    std::ofstream file(m_settingsPath);
    if (!file.is_open()) {
        return false;
    }

    file << "# FLTorrent Settings\n\n";
    
    for (const auto& pair : m_settings) {
        file << pair.first << "=" << pair.second << "\n";
    }

    file.close();
    return true;
}

void SettingsManager::setDefaults() {
    // General
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        std::string downloadPath = std::string(path) + "\\Downloads";
        setDefaultSavePath(downloadPath);
    } else {
        setDefaultSavePath("C:\\Downloads");
    }
#else
    setDefaultSavePath(std::string(getenv("HOME")) + "/Downloads");
#endif
    setStartWithSystem(false);
    setMinimizeToTray(false);
    
    // Network
    setMaxDownloadRate(0); // Unlimited
    setMaxUploadRate(0); // Unlimited
    setMaxConnections(200);
    setListenPort(6881);
    
    // BitTorrent
    setDHTEnabled(true);
    setPEXEnabled(true);
    setLSDEnabled(true);
    setUPnPEnabled(true);
    
    // UI
    setWindowWidth(1024);
    setWindowHeight(768);
    setWindowX(100);
    setWindowY(100);
    setWindowMaximized(false);
    setDarkMode(false);
    
    // Advanced
    setUserAgent("FLTorrent/0.1.0");
}

std::string SettingsManager::getConfigPath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\FLTorrent\\settings.ini";
    }
    return "settings.ini";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/fltorrent/settings.ini";
    }
    return "settings.ini";
#endif
}

void SettingsManager::parseLine(const std::string& line) {
    std::string trimmed = trim(line);
    
    // Skip empty lines and comments
    if (trimmed.empty() || trimmed[0] == '#') {
        return;
    }

    // Find '=' separator
    size_t pos = trimmed.find('=');
    if (pos == std::string::npos) {
        return;
    }

    std::string key = trim(trimmed.substr(0, pos));
    std::string value = trim(trimmed.substr(pos + 1));
    
    m_settings[key] = value;
}

std::string SettingsManager::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// Generic getters/setters
std::string SettingsManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_settings.find(key);
    return (it != m_settings.end()) ? it->second : defaultValue;
}

void SettingsManager::setString(const std::string& key, const std::string& value) {
    m_settings[key] = value;
}

int SettingsManager::getInt(const std::string& key, int defaultValue) const {
    std::string value = getString(key);
    if (value.empty()) {
        return defaultValue;
    }
    return std::stoi(value);
}

void SettingsManager::setInt(const std::string& key, int value) {
    m_settings[key] = std::to_string(value);
}

bool SettingsManager::getBool(const std::string& key, bool defaultValue) const {
    std::string value = getString(key);
    if (value.empty()) {
        return defaultValue;
    }
    return value == "true" || value == "1";
}

void SettingsManager::setBool(const std::string& key, bool value) {
    m_settings[key] = value ? "true" : "false";
}

// Specific getters/setters
std::string SettingsManager::getDefaultSavePath() const {
    return getString("DefaultSavePath");
}

void SettingsManager::setDefaultSavePath(const std::string& path) {
    setString("DefaultSavePath", path);
}

bool SettingsManager::getStartWithSystem() const {
    return getBool("StartWithSystem");
}

void SettingsManager::setStartWithSystem(bool value) {
    setBool("StartWithSystem", value);
}

bool SettingsManager::getMinimizeToTray() const {
    return getBool("MinimizeToTray");
}

void SettingsManager::setMinimizeToTray(bool value) {
    setBool("MinimizeToTray", value);
}

int SettingsManager::getMaxDownloadRate() const {
    return getInt("MaxDownloadRate");
}

void SettingsManager::setMaxDownloadRate(int rate) {
    setInt("MaxDownloadRate", rate);
}

int SettingsManager::getMaxUploadRate() const {
    return getInt("MaxUploadRate");
}

void SettingsManager::setMaxUploadRate(int rate) {
    setInt("MaxUploadRate", rate);
}

int SettingsManager::getMaxConnections() const {
    return getInt("MaxConnections");
}

void SettingsManager::setMaxConnections(int connections) {
    setInt("MaxConnections", connections);
}

int SettingsManager::getListenPort() const {
    return getInt("ListenPort");
}

void SettingsManager::setListenPort(int port) {
    setInt("ListenPort", port);
}

bool SettingsManager::getDHTEnabled() const {
    return getBool("DHTEnabled");
}

void SettingsManager::setDHTEnabled(bool enabled) {
    setBool("DHTEnabled", enabled);
}

bool SettingsManager::getPEXEnabled() const {
    return getBool("PEXEnabled");
}

void SettingsManager::setPEXEnabled(bool enabled) {
    setBool("PEXEnabled", enabled);
}

bool SettingsManager::getLSDEnabled() const {
    return getBool("LSDEnabled");
}

void SettingsManager::setLSDEnabled(bool enabled) {
    setBool("LSDEnabled", enabled);
}

bool SettingsManager::getUPnPEnabled() const {
    return getBool("UPnPEnabled");
}

void SettingsManager::setUPnPEnabled(bool enabled) {
    setBool("UPnPEnabled", enabled);
}

int SettingsManager::getWindowWidth() const {
    return getInt("WindowWidth");
}

void SettingsManager::setWindowWidth(int width) {
    setInt("WindowWidth", width);
}

int SettingsManager::getWindowHeight() const {
    return getInt("WindowHeight");
}

void SettingsManager::setWindowHeight(int height) {
    setInt("WindowHeight", height);
}

int SettingsManager::getWindowX() const {
    return getInt("WindowX");
}

void SettingsManager::setWindowX(int x) {
    setInt("WindowX", x);
}

int SettingsManager::getWindowY() const {
    return getInt("WindowY");
}

void SettingsManager::setWindowY(int y) {
    setInt("WindowY", y);
}

bool SettingsManager::getWindowMaximized() const {
    return getBool("WindowMaximized");
}

void SettingsManager::setWindowMaximized(bool maximized) {
    setBool("WindowMaximized", maximized);
}

bool SettingsManager::getDarkMode() const {
    return getBool("DarkMode");
}

void SettingsManager::setDarkMode(bool enabled) {
    setBool("DarkMode", enabled);
}

std::string SettingsManager::getUserAgent() const {
    return getString("UserAgent");
}

void SettingsManager::setUserAgent(const std::string& agent) {
    setString("UserAgent", agent);
}
