# FTorrent - Project Architecture

## Toolbar and assets: where things live

Two small modules own the parts that are pure policy, so a change to either
lands in one file and can be reasoned about on its own.

**`AssetLoader`** (`src/AssetLoader.{h,cpp}`) owns *file-based* assets: where
`assets/` lives (resolved from the running executable) and how one PNG becomes a
drawable image. It is not `Resources`, which owns what is compiled INTO the
binary (the XPM set and the logo); two different stores, two different owners.
Every icon load goes through `AssetLoader::load(name, size)`, which returns
`nullptr` when the file is missing — the caller then keeps whatever fallback it
had. Do not open a PNG anywhere else: a `d() == 0` guard does not work (FLTK
reports `d() == 3` for a missing file), which is why the guard lives here once.

Two different icon sizes would show up as two different-looking buttons in the
same bar, so there is one number for both stores: `ToolbarLayout::kIconSize`.
`AssetLoader` loads the PNGs at it, and `Resources::rescaleGlyph()` resamples
the compiled-in glyphs to it. The resampling has to live in `Resources` — a
`Fl_Pixmap::copy()` at the call site would be nearest-neighbour, and on 16x16
art with 4px strokes that turns a pause bar into irregular 7x18 blocks.
`rescaleGlyph()` box-filters instead and returns an RGBA image so the soft edge
blends with the button behind it. Note that **both** `Fl_RGB_Image` constructors
that take raw pixels only alias the buffer they are given (`alloc_array` stays
`0`), so the buffer has to outlive the image.

**`ToolbarLayout`** (`src/ToolbarLayout.{h,cpp}`) owns the level-of-detail
policy: which level of label detail fits in a given width, how wide each button
is at that level, and what happens to the room going spare. It is pure data plus
arithmetic — no widgets. The required width of a level is *derived* from the
same table that `MainWindow` applies, so a button cannot be resized without the
level that contains it following. `MainWindow::layoutToolbar()` only moves
widgets.

Three things about the bar are Fl_Pack behaviour, not policy, and are easy to
break by accident:

- The pack **shrinks to the width of its children**, so centring needs a spacer
  at *both* ends (`m_leadSpacer`, `m_trailSpacer`). With only a leading spacer
  the pack shrinks, sits at the left of the window and the block ends up centred
  inside itself.
- The pack **stretches every child to its own height**, so the *pack* is sized to
  `kButtonHeight` and the taller `kBarHeight` strip is a plain group around it.
  Sizing the pack to the bar height instead pins every button to the bar's edge
  with no padding, which is what it used to do.
- The pack **skips a hidden child together with the gap on either side of it**.
  `requiredWidth()` counts `kChildCount - 1` gaps, which is only true while all
  thirteen children are in the pack, so a spacer that has collapsed to zero
  width stays *visible* and simply draws nothing. Hiding it takes two gaps out
  of the row: at 720px the pack came up 11px short of the window and the block
  sat against the left edge with the shortfall as dead bar on the right.
  `ToolbarLayout::spacerStaysVisible()` is that rule, and `setSpacer()` in
  `MainWindow` asks it rather than repeating the reasoning.

`kChildCount` is *derived* — the same terms `requiredWidth()` sums, grouped as
labelled buttons, icon buttons, group spacers, other fixed widgets and the
elastic pair — so a new widget has to be added to a group rather than beside the
number. Derivation is not enough on its own, though: the pack is built out of
individual `new` calls and nothing in the types connects them. So
`MainWindow::createToolbar()` compares `m_toolbarPack->children()` against
`kChildCount` immediately after the pack is finished and **aborts with both
numbers** if they disagree. A fourteenth widget used to produce a bar a few
pixels out of centre with no error anywhere; it now refuses to start.

The slack is then spent exactly: `growPerButton(slack) * kGrowableCount +
2 * margin(slack) == slack`. Two equal margins can only be an even number of
pixels, so when the fair share leaves an odd one over, the buttons give it back
and it becomes margin. Rounding it away instead leaves a pixel of dead bar, and
that pixel is enough to make the block visibly off-centre at the narrowest
width the window allows. The one slack no pair of widths can express is 1, so
the row is a single pixel short at exactly three window widths (477, 645, 789).

State ownership for the toolbar button itself:

| State | Owner | Notes |
|---|---|---|
| Limit on/off | `MainWindow::m_limitModerate` | session only, never written to settings |
| Background when off | `MainWindow::m_limitIdleColor` | captured once at creation, so it matches the toolbar |
| Level of detail | `MainWindow::m_toolbarLevel` | derived from the window width via `ToolbarLayout` |
| Eye icons | `MainWindow::reloadEyeIcons()` | the only images that follow the theme |
| Pause/play/remove glyphs | `Resources` | resampled to `kIconSize` at `initialize()`; `updateToolbar()` only chooses between pause and play |
| Icon size | `ToolbarLayout::kIconSize` | the one number both asset stores draw at |
| Centring margins | `MainWindow::layoutToolbar()` | written to both spacers from `ToolbarLayout::margin()`; a spacer is shown even at zero width, because a hidden one takes its gaps with it |
| Last layout applied | `MainWindow::m_toolbarLevel` + `m_toolbarSlack` | the level alone is not enough: widening the window inside one level still has to re-centre |
| Public IP in the status bar | `SettingsManager::ShowPublicIp` | **off by default**: showing it makes `TorrentManager::update()` ask an external service for the address, so a fresh install contacts nothing until Preferences turns it on. The default lives in `setDefaults()` *and* in the `getBool` fallback, which have to agree; once the key is in `settings.ini` the stored value wins, so changing the default never overrides a choice the user already made |

Data flow: window width → `ToolbarLayout::forWidth()` → `Spec` →
`MainWindow::layoutToolbar()` moves widgets → `applyLimitStyle()` paints the
limit button. The theme colour of every widget is still applied by
`MainWindow::applyTheme()`, `SearchResultsWidget::applyTheme()` and
`TorrentListWidget::refreshThemeColors()`; consolidating those three into one
palette is the obvious next step, and it is not done yet.

## Tests

`tests/` holds four programs that build the REAL `MainWindow` and assert against
the real widgets, so nothing here reads a screenshot and all of it runs headless:

| suite | what it holds down |
|---|---|
| `toolbar_layout` | every window width from 400 to 1920: the pack spans the window, the two margins are equal and match `margin(slack) + kSpacing`, and the level on the buttons is the one `forWidth()` promised — plus the level thresholds at each boundary and one pixel below it |
| `toolbar_clicks` | every toolbar button clicked at 720px, the narrowest width `size_range()` allows, and the bar still centred afterwards |
| `icons` | the toolbar icons, the level/width/padding rules and the centring, in both the assets-present and assets-missing variants |
| `flows` | the user journeys end to end: preferences, add, remove, the limit toggle, a live search and its cancel path, and a real torrent through pause and resume. Labelled `network` |

They link `ftorrent_core`, the same object library the app is built from, so a
test exercises the objects that ship rather than a second copy that can drift.

Off by default, because a normal build should be a plain application build:

```
cmake -S . -B build -DFTORRENT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -LE network      # skip the tests that need the internet
```

Two things worth knowing if you write one:

- **Never touch the developer's config.** `test_support.h`'s `useScratchConfig()`
  points the app at a scratch directory via `XDG_CONFIG_HOME` (or `APPDATA`) and
  removes it afterwards. The earlier out-of-tree probes ran
  `rm -rf ~/.config/ftorrent`, which is not something a test may do to the
  machine it runs on. It also creates the directory the app *actually* uses —
  `SystemUtils::getConfigDir()` appends a suffix to the variable, and
  `SettingsManager::save()` opens the file without creating a directory, so a
  missing one turns every save into a silent no-op.
- **Let the container draw before reading positions.** `Fl_Pack` places its
  children while it draws, so `test_support.h`'s `forceLayout()` renders into an
  offscreen surface first. Reading coordinates without it gets you stale ones.

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
