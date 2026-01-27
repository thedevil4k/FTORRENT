# FTorrent - Project Architecture

## 📐 Overview

FTorrent is designed with a modular, object-oriented architecture that clearly separates responsibilities:

```
┌─────────────────────────────────────────┐
│           FLTK UI Layer (UI)            │
│          (main.cpp, widgets)            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│         TorrentManager (Facade)         │
│      - Coordinates operations           │
│      - Callback system                  │
└──────┬──────────────────────────────────┘
       │
       ├──────▶ TorrentSession
       │        (libtorrent Wrapper)
       │
       ├──────▶ TorrentItem
       │        (Individual Torrent Model)
       │
       └──────▶ SettingsManager
                (Global Configuration)
```

## 📦 Main Components

### 1. TorrentSession
**File:** `TorrentSession.h/cpp`

**Responsibility:** Low-level wrapper over libtorrent-rasterbar

**Features:**
- libtorrent session initialization and configuration
- Basic operations: add/remove torrents
- libtorrent alert processing
- Retrieval of global statistics

**Main API:**
```cpp
bool initialize();
bool addTorrentFile(const std::string& file, const std::string& path);
bool addMagnetLink(const std::string& magnet, const std::string& path);
std::vector<lt::torrent_handle> getTorrents();
void processAlerts();
```

---

### 2. TorrentItem
**File:** `TorrentItem.h/cpp`

**Responsibility:** Data model for an individual torrent

**Features:**
- Encapsulates all information of a torrent
- Caches data for performance
- Provides formatting methods (size, speed, time)
- Predefined states: Queued, Checking, Downloading, Seeding, Paused, Error, Complete

**Data managed:**
- Basic info: name, hash, path
- Progress: total size, downloaded, progress %
- Speeds: download/upload rate
- Peers: number of peers and seeds
- Times: ETA, added time, completed time
- Upload ratio

**Main API:**
```cpp
void update(); // Update from libtorrent
std::string getName();
double getProgress();
int getDownloadRate();
State getState();
std::string formatSize(int64_t bytes);
```

---

### 3. TorrentManager
**File:** `TorrentManager.h/cpp`

**Responsibility:** Central manager that coordinates everything

**Features:**
- Main facade for the UI
- Maintains a list of TorrentItems
- Callback system to notify changes
- Automatic synchronization with libtorrent
- High-level operations on torrents

**Callback System:**
```cpp
using TorrentAddedCallback = std::function<void(TorrentItem*)>;
using TorrentRemovedCallback = std::function<void(const std::string& hash)>;
using TorrentUpdatedCallback = std::function<void(TorrentItem*)>;
using StatsUpdatedCallback = std::function<void()>;
using ErrorCallback = std::function<void(const std::string& error)>;
```

**Main API:**
```cpp
bool initialize();
bool addTorrentFile(const std::string& file, const std::string& path);
void removeTorrent(const std::string& hash, bool deleteFiles);
void pauseTorrent(const std::string& hash);
void resumeTorrent(const std::string& hash);
std::vector<TorrentItem*> getAllTorrents();
void update(); // Regular call from UI timer
```

---

### 4. SettingsManager
**File:** `SettingsManager.h/cpp`

**Responsibility:** Persistent configuration management

**Features:**
- Singleton pattern
- Load/save configuration in INI file
- Reasonable default values
- Cross-platform configuration (Windows/Linux)

**Settings Categories:**
- **General:** download path, start with system
- **Network:** rate limits, port, connections
- **BitTorrent:** DHT, PEX, LSD, UPnP
- **UI:** window position and size
- **Advanced:** user agent, custom configurations

**Main API:**
```cpp
static SettingsManager& instance(); // Singleton
bool load();
bool save();
std::string getDefaultSavePath();
int getMaxDownloadRate();
bool getDHTEnabled();
// ... many more getters/setters
```

---

## 🔄 Data Flow

### Adding a Torrent:
```
UI → TorrentManager::addTorrentFile()
   → TorrentSession::addTorrentFile()
   → libtorrent adds the torrent
   → TorrentManager::syncTorrents()
   → Creates new TorrentItem
   → Callback: onTorrentAdded(item)
   → UI updates the list
```

### Periodic Update:
```
Timer → TorrentManager::update()
      → TorrentSession::processAlerts()
      → TorrentManager::syncTorrents()
      → For each TorrentItem::update()
      → Callback: onTorrentUpdated(item)
      → UI updates speeds/progress
```

### Configuration:
```
UI Settings Dialog → SettingsManager::setMaxDownloadRate(500)
                    → SettingsManager::save()
                    → INI file updated
```

---

## 🎯 Advantages of this Architecture

1. **Separation of Concerns:** Each class has a clear purpose.
2. **Testable:** Each component can be tested independently.
3. **Extensible:** Easy to add new features.
4. **Callbacks:** UI reacts to changes without constant polling.
5. **Persistent Configuration:** Settings survive restarts.
6. **Abstraction:** UI doesn't need to know libtorrent details.
7. **Thread Safety:** Dedicated worker thread ensures smooth UI performance.

---

## 🚀 Advanced Features Implemented

### UI Components:
- ✅ `MainWindow` - Main window with toolbar and status bar
- ✅ `TorrentListWidget` - Multi-column sortable table
- ✅ `TorrentDetailsDialog` - Detailed stats, peers, and file lists
- ✅ `AddTorrentDialog` - File/Magnet support with preview
- ✅ `PreferencesDialog` - Comprehensive settings management

### Performance & Integration:
- ✅ **System Tray:** Full integration with Windows tray icons.
- ✅ **Real-time Engine:** Immediate application of bandwidth limits.
- ✅ **Resource Tracking:** Real-time RAM and throughput monitoring.
- ✅ **Zero Blocking:** Decoupled alert processing for high responsiveness.

---

## 📝 Implementation Notes

- **Thread Safety:** TorrentManager uses a dedicated thread for libtorrent alerts, with results posted back to the UI thread safely.
- **UI Update:** FLTK timers (100ms) ensure the UI stays synchronized with the backend.
- **Performance:** XPM icons are embedded to minimize disk I/O and bundle size.
- **Memory:** Strict RAII with smart pointers throughout the codebase.
