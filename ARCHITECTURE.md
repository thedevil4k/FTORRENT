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

`tests/` holds the test programs that build the REAL `MainWindow` and assert
against the real widgets, so nothing here reads a screenshot and all of it runs
headless:

| suite | what it holds down |
|---|---|
| `toolbar_layout` | every window width from 400 to 1920: the pack spans the window, the two margins are equal and match `margin(slack) + kSpacing`, and the level on the buttons is the one `forWidth()` promised — plus the level thresholds at each boundary and one pixel below it |
| `toolbar_clicks` | every toolbar button clicked at 720px, the narrowest width `size_range()` allows, and the bar still centred afterwards |
| `icons` | the toolbar icons, the level/width/padding rules and the centring, in both the assets-present and assets-missing variants |
| `flows` | the user journeys end to end: preferences, add, remove, the limit toggle, a live search and its cancel path, and a real torrent through pause and resume. Labelled `network` |
| `security_analyzer` | metadata heuristics: file families, hostile names, symlinks, torrent-level counts and the non-verdict wording |
| `security_badge` | risk labels and palette, name sanitising and the shield rendered offscreen |
| `security_keys` | the key store: round trip, removal and the absence of the key from the stored bytes |
| `reputation` | provider parsing from canned answers, the service's states, the choice of hash, and the exact no-hash sentence |
| `archive_preflight` | piece arithmetic and a real ZIP/RAR5 listing rebuilt from only its first and last piece |

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

## Security analysis before downloading

The security path is deliberately split into three small pieces, none of which
downloads a byte of content.

**`SecurityTypes`** (`src/SecurityTypes.h`) is the shared plain-data model:
`SecurityFile`, `Finding`, `FileAssessment` and `SecurityReport`. It has no
libtorrent and no FLTK types, so the same record can be filled by
`TorrentFileList` (search metadata), `TorrentItem` (an active torrent) or a
test. `TorrentFileEntry` is an alias of `SecurityFile`; the old name stays
because it is the public type of the file-list fetcher.

**`SecurityAnalyzer`** (`src/SecurityAnalyzer.{h,cpp}`) is pure and takes a
`const std::vector<SecurityFile>&`. It classifies files by extension and by the
metadata flags libtorrent already exposes, then produces per-file findings and
a torrent-level report:

| severity | examples |
|---|---|
| Medium | native executables, installers, archives, disk images |
| High | scripts, shortcuts, macro documents, reserved device names, `autorun.inf`, zero-byte executables, hidden files that can run |
| Critical | double extensions, bidi overrides, path traversal, absolute/UNC paths, symlinks |

`Unknown` is a real state: with no file list the analyzer returns it instead of
assuming a clean result. A report with no findings says **“No known indicators
in file names or metadata”** and never “safe”; the module has no network code
and never opens a file.

**`SecurityBadge`** (`src/SecurityBadge.{h,cpp}`) owns the presentation rules:
the risk palette, the shield geometry, the `SecurityShieldBox` widget and the
`displayText()` sanitiser that replaces control and bidi characters before an
attacker-controlled name reaches a row. There is deliberately no green “safe”
state.

Where the report is consumed:

- `SearchResultsWidget` fetches the file list on row expansion (through
  `FileListService`), analyzes it once, caches the report by infohash and draws
  the shield, the summary line and per-file tags under the expanded name.
- `AddTorrentDialog` analyzes the files that are currently checked. Its
  **Select only non-executables** button unchecks everything
  `SecurityAnalyzer::isRunnable()` considers code, while leaving archives and
  disk images checked. A High/Critical selection needs explicit confirmation.
- `TorrentListWidget` caches the report by infohash and draws the shield next
  to the name.
- `TorrentDetailsDialog` has a Security tab with the full report and tags each
  row of the Files tab with the same analysis.

File priorities are indexed by libtorrent's `file_index_t`, not by visible row.
Pad files occupy indices but have no row, so both the Add dialog and the magnet
handoff in `MainWindow` build a full-size priority vector and leave pad gaps at
zero. Getting this wrong shifts every later file by one.

The analysis runs on the UI thread because it is pure string/flag work over a
few thousand entries. Fetching the metadata itself still runs in the
`FileListService` worker.

## Reputation and archive preflight

**`SecurityHashes`** decides whether a hash may be sent to a provider at all.
A BitTorrent v2 file stores the root of a Merkle tree built from 16 KiB blocks,
and that root equals the SHA-256 of the file only when the file is a single
block. Anything larger is a Merkle root, so it is deliberately left without a
hash: a wrong digest would produce a confident "no record" for a file that is
known to be bad. That is why `SecurityFile::sha256Complete` exists next to
`sha256`.

**`Reputation`** holds the providers. Each one is a `ReputationProvider` and is
queried with the 64 hex characters of a hash and nothing else. VirusTotal uses
`GET /api/v3/files/<hash>` with an `x-apikey` header; MalwareBazaar uses a form
`POST` to `mb-api.abuse.ch` with an `Auth-Key` header. Both headers are set from
headers the user stored; a provider without one is skipped rather than asked
anonymously. (abuse.ch began requiring the Auth-Key; a live check answered 401
without it, which is why the "open service" assumption in the first version of
this module was wrong.) Both go through `HttpClient`, which now supports
request headers and a body, and both refuse to follow redirects: a redirect
would replay the API key to a host the user never chose. Answers are parsed by
a small JSON reader with a depth limit and no tolerance for malformed input, so
a broken reply becomes an error rather than a fabricated verdict.

**`SecurityKeys`** stores those keys outside `settings.ini`: DPAPI
(`CryptProtectData`) on Windows, and a 0600 file inside a 0700 directory on
Linux, created with `open()` rather than written and then chmod'ed.

**`ReputationService`** is the only thing that touches the network, and it never
starts on its own. A lookup exists because a shield was clicked. It caches per
hash, keeps several waiters per in-flight hash (so the same click from two
surfaces is one request), and delivers on the UI thread through `Fl::awake()`.
`NoCompleteHash` is a first-class state carrying the exact sentence the product
requires.

**`SecurityReputation`** picks the file a click should ask about (a runnable
file wins over a picture) and words the answer. **`ArchivePreflight`** is the
level-2 path: it computes which pieces hold a file's first and last byte from
`file_offset()`/`file_size()`, fetches only those with `set_piece_deadline(...,
alert_when_available)` in a throwaway session and directory, writes them into a
sparse file of the real size, and lets `ArchiveInspector::listEntries()` read
the index. The directory is removed before the call returns.

Two entry points, one body: `ArchivePreflight` accepts either a magnet or
`.torrent` path (search rows, Add Torrent) or an `add_torrent_params` carrying
the metadata (Details). The second one means a torrent the app already knows
does not exchange metadata with the swarm a second time. The three surfaces
share `archiveSummary()` for the wording and each keeps its own result line;
the search-row line is hit-tested from the rectangle the draw recorded, and the
archive worker reports through `Fl::awake()` behind the same `alive` flag the
reputation lookups use.

**Local hashing.** When the metadata has no whole-file hash, Details asks
`findLocalHashTarget()` for a file that is complete, can run, and whose recorded
path stays inside the torrent (no absolute path, no symlink, no `..` segment).
If there is one, `FileHash::sha256File()` runs on a worker with a cancel flag the
destructor sets, and the digest is then looked up like any other. The answer
says the SHA-256 was computed locally, because a hash the user did not expect to
be computed is a surprise worth naming.

## Network monitor

Two pieces, both fed only by what libtorrent reports.

**`SecurityEvents`** is a bounded, mutex-guarded ring buffer of `SecurityEvent`
(time, torrent, kind, detail). `TorrentSession::processAlerts()` appends peer
bans, peer errors, invalid requests, torrent/file errors and tracker failures as
it drains the alert queue. The cap (300) drops the oldest event, because a
monitor that forgets the newest one is useless.

**`NetworkMonitorDialog`** reads `TorrentManager::getAllTorrents()` and each
`TorrentItem::getPeers()`. `PeerInfo` carries port, direction and encryption
next to the fields the Details window already used. The dialog rebuilds only the
visible tab on its one-second timer, and appends new events instead of
rebuilding the list.

There is deliberately no packet capture: showing traffic would need raw sockets
or libpcap, a different privilege level and a different failure mode. The
dialog says what it shows.

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
