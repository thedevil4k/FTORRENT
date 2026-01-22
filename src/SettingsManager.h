#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <string>
#include <map>
#include <memory>

/**
 * @brief Application settings manager
 * 
 * Handles loading, saving and access to FTorrent settings.
 * Uses a simple INI file to store user preferences.
 */
class SettingsManager {
public:
    // Singleton pattern
    static SettingsManager& instance();

    // Load/Save settings
    bool load();
    bool save();
    
    // General settings
    std::string getDefaultSavePath() const;
    void setDefaultSavePath(const std::string& path);
    
    bool getStartWithSystem() const;
    void setStartWithSystem(bool value);
    
    bool getMinimizeToTray() const;
    void setMinimizeToTray(bool value);
    
    // Network settings
    int getMaxDownloadRate() const; // KB/s, 0 = unlimited
    void setMaxDownloadRate(int rate);
    
    int getMaxUploadRate() const; // KB/s, 0 = unlimited
    void setMaxUploadRate(int rate);
    
    int getMaxConnections() const;
    void setMaxConnections(int connections);
    
    int getListenPort() const;
    void setListenPort(int port);
    
    // BitTorrent settings
    bool getDHTEnabled() const;
    void setDHTEnabled(bool enabled);
    
    bool getPEXEnabled() const;
    void setPEXEnabled(bool enabled);
    
    bool getLSDEnabled() const;
    void setLSDEnabled(bool enabled);
    
    bool getUPnPEnabled() const;
    void setUPnPEnabled(bool enabled);
    
    // UI settings
    int getWindowWidth() const;
    void setWindowWidth(int width);
    
    int getWindowHeight() const;
    void setWindowHeight(int height);
    
    int getWindowX() const;
    void setWindowX(int x);
    
    int getWindowY() const;
    void setWindowY(int y);
    
    bool getWindowMaximized() const;
    void setWindowMaximized(bool maximized);
    
    bool getDarkMode() const;
    void setDarkMode(bool enabled);
    
    // Advanced settings
    std::string getUserAgent() const;
    void setUserAgent(const std::string& agent);
    
    // Generic getter/setter for custom values
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    void setString(const std::string& key, const std::string& value);
    
    int getInt(const std::string& key, int defaultValue = 0) const;
    void setInt(const std::string& key, int value);
    
    bool getBool(const std::string& key, bool defaultValue = false) const;
    void setBool(const std::string& key, bool value);

private:
    SettingsManager();
    ~SettingsManager();
    
    // Prevent copying
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    std::string m_settingsPath;
    std::map<std::string, std::string> m_settings;
    
    void setDefaults();
    std::string getConfigPath() const;
    
    // Helper methods
    void parseLine(const std::string& line);
    std::string trim(const std::string& str) const;
};

#endif // SETTINGSMANAGER_H
