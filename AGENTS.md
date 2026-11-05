# AGENTS.md - working on FTorrent

FTorrent is a BitTorrent client in C++20 (FLTK for the UI, libtorrent for the
engine, libarchive for archives). Windows is the primary target, Linux and macOS
build from the same sources. This file is the contract for any change: read it
before editing, and update it when a rule changes.

## Product invariants

These are not style preferences. A change that breaks one is a bug even if it
compiles and the tests pass.

- **A clean result says "no known indicators", never "safe".** Unknown stays
  "unknown".
- **No uploads and no telemetry.** Nothing the user downloads or checks leaves
  the machine.
- **Providers are asked only after a click.** VirusTotal and MalwareBazaar are
  never queried on their own.
- **No API key ships with the binary.** Keys belong to the user, are stored with
  DPAPI (Windows) or a 0600 file (Linux), and a provider without a key is
  skipped, not asked anonymously.
- **Never download a whole file to hash it.** When the metadata carries no
  whole-file SHA-256 and the file is not already on disk, the answer is exactly
  `VT no disponible antes de descargar el archivo completo.`
- **The archive check never downloads the archive.** ZIP and RAR5 are listed
  from two pieces; RAR 1.5-4.x is walked header by header under a budget and the
  result is labelled **partial**. A partial list never claims to be complete.
- **The small print always shows the hash** the answer was based on, or says the
  metadata carried none.
- **Badges**: the shield sits next to the torrent name in search results, Add
  Torrent, the torrent list and Details. Content badges appear in search results
  only, in the order compressed, encrypted lock, ISO, video, executable, in the
  same 15 px box as the shield.
- **No streaming**: `play.png` stays unused because the app does not stream
  torrents.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFTORRENT_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -LE network   # offline suite
ctest --test-dir build --output-on-failure -L unit       # headless subset
```

On this machine (MSYS2/MinGW-w64) CMake and the compiler live under
`C:/msys64/mingw64/bin/`. The test labels are `unit` (headless, offline), `ui`
(FLTK, no network) and `network` (real trackers/DHT; `flows` and
`filelist_network`). `archive_preflight_network` is also labelled `network`,
but it is a loopback test: a seeder in the same process on 127.0.0.1, so
nothing leaves the machine. It runs every scenario in a child process, because
libtorrent 2.1.2 (the version this machine has) has an intermittent
use-after-free of its own: a session that is torn down while its disk thread
is still working dies with an access violation in a background thread. It
reproduces with a program that contains no FTorrent code, so it is not this
code's bug. A child that dies that way is retried, and a child that keeps
dying fails the test with that as the reason, so an upstream crash is never
mistaken for a broken archive check.

Tests link `ftorrent_core`, the same object library the application is built
from. There is no second copy of the code under `tests/`.

## Where things live

`src/` is flat and no file grows past roughly 800 lines. A file that outgrew
that is split by topic: whole functions move unchanged into a new file that
answers one question, and the class stays one class with one header. The
private members the new files touch stay in that header.

| area | files |
| :--- | :--- |
| Entry point and wiring | `src/main.cpp`, `src/MainWindow.*` (the window, its search view, its theme, its actions and its tray) |
| Torrent engine | `src/TorrentSession.*`, `src/TorrentManager.*`, `src/TorrentItem.*` |
| Search | `src/SearchEngine.*` (the engine, the page reader and the catalogue), `src/SearchScope.*`, `src/SearchResultsWidget.*` (the table, its badges, its archive line and its file list), `src/SearchManager.*` |
| File list of a magnet | `src/TorrentFileList.*` (metadata over the swarm), `src/FileListService.*` (queued, one worker per row) |
| Security analysis | `src/SecurityAnalyzer.*` (metadata rules), `src/SecurityTypes.h`, `src/SecurityBadge.*`, `src/SecurityEvents.*` |
| Reputation | `src/Reputation.*` (providers/parsers), `src/ReputationService.*` (cache, one request per hash), `src/SecurityReputation.*` (which hash, wording), `src/SecurityKeys.*` |
| Archive check | `src/ArchivePreflight.*` (what to read and what to say), `src/ArchiveProbeSession.*` (the throwaway libtorrent session that fetches pieces), `src/ArchiveInspector.*` (libarchive), `src/ArchiveCheckTask.*` (worker + UI delivery) |
| HTTP | `src/HttpClient.*` (WinInet on Windows, libcurl elsewhere) |
| UI shell | `src/PreferencesDialog.*`, `src/AddTorrentDialog.*`, `src/TorrentDetailsDialog.*`, `src/TorrentListWidget.*`, `src/NetworkMonitorDialog.*` |
| Policies without FLTK | `src/ToolbarLayout.*`, `src/SecurityHashes.*`, `src/PathUtils.*`, `src/Format.*` |

The window logo is the one binary payload in the tree. The PNG
(`src/assets/window-logo.png`) is the source of truth and CMake turns it into
the C array that `Resources.cpp` hands to FLTK, so the logo stays compiled
into the binary and the source tree holds a picture instead of a wall of hex.

## One rule, one place

Never add a second list or predicate for a question the code already answers.
The owner of each rule:

| question | owner |
| :--- | :--- |
| Can this file run? | `SecurityAnalyzer::isRunnable()` (executables, installers, scripts, shortcuts, macros) |
| Is this an archive (risk classification)? | `SecurityAnalyzer::isArchivePath()` |
| Can its index be read before downloading? | `ArchiveInspector::canListBeforeDownload()` |
| Which hash may be sent to a provider? | `SecurityHashes`, `findReputationTarget()` |
| What does a shield say? | `SecurityBadge` |
| What does an archive check print? | `archiveSummary()` + `archiveCheckMessage()` in `SecurityReputation.*` |
| How is a size printed? | `Format::bytes()` / `Format::perSecond()` |
| Which libtorrent settings does a session use? | `TorrentSession` (download) and `ArchiveProbeSession` (probe) |

## Conventions

- **Language:** identifiers and comments in English. User-facing strings are
  English; the one exception is the exact sentence above, which is product copy.
- **Comments explain why**, especially where a libtorrent or FLTK behaviour is
  surprising. Do not restate the code.
- **Names:** `PascalCase` types, `camelCase` functions and locals, `m_` members,
  `SCREAMING_SNAKE` constants. Files are named after their primary type.
- **Formatting:** 4 spaces, 80 columns, one statement per line. `.clang-format`
  describes the current style; do not reformat a whole file in a behaviour
  commit.
- **Errors:** a library function returns `false` and fills a human-readable
  reason; UI code prefixes it with what was being attempted. Do not swallow an
  exception without a comment saying why the failure is irrelevant.
- **Threads:** a worker never touches FLTK. It posts to the UI thread with
  `Fl::awake()` and the receiver checks a `std::shared_ptr<bool> alive` flag
  first. Reuse `ArchiveCheckTask`, `FileListService` or `ReputationService`
  instead of writing a fourth copy of that pattern.
- **Never introduce a magic timeout or size**: give it a named constant where it
  is used and say what it protects.
- **A throwaway libtorrent session is closed in one order**: the torrent is
  removed, the removal is waited for (the handle stops being valid once
  libtorrent's own I/O tasks are done with it), and only then is the session
  destroyed and its files deleted. libtorrent keeps the torrent alive while
  that work is queued, and a session torn down before it is gone leaves the
  disk thread working on freed memory. `ArchiveProbeSession::shutdown()` is
  that sequence; use it instead of writing another one.
- **A probe that already knows its peers does not join the DHT.** The
  discovery is off when the caller handed the session its endpoints, which
  also keeps the probe from talking to the public routers.

## Recipes

**Add an archive format to the pre-download check**

1. Extend `ArchiveInspector::canListBeforeDownload()` and enable the reader in
   `openArchive()` and `listEntriesFromWindows()`.
2. Decide which rule applies: two pieces (index at both ends), a bounded walk
   (headers interleaved), or not listable. Implement it in
   `ArchivePreflight::inspect()`.
3. If it is a bounded walk, `ArchiveProbeSession::setPieceBudget()` and the
   partial note are mandatory; the result must never look complete.
4. Add a fixture under `tests/fixtures/archives/` with its source URL and
   licence in the README there, plus an offline test in `archive_preflight`.
5. Update the format table in `README.md` and the portable README.

**Add or change a content badge**

1. The predicate goes in `SecurityAnalyzer` (one place).
2. The asset and the drawing go in `SecurityBadge`; the order and the 15 px box
   are set there, not in the widgets.
3. Search rows are the only surface that shows content badges. Add the case in
   `SearchResultsWidget::badgeFor()` if the mapping is not automatic.
4. Extend `tests/security_badge.cpp`.

**Add a reputation provider**

1. Implement `ReputationProvider` in `src/Reputation.cpp` (request, parse,
   wording) and register it in the service.
2. Its key, if any, is a `SecurityKeys::Provider`; never a literal.
3. Redirects stay off for provider calls: a redirect would replay the key.
4. Add canned-answer tests in `tests/reputation.cpp`; no network.

**Add a test**

1. `tests/CMakeLists.txt` lists sources by hand: add the executable with
   `ftorrent_add_test()`, register it with `add_test()`, and give it a label.
2. Prefer a headless `unit` test. If it needs FLTK, use `tests/test_support.h`
   (scratch config + offscreen layout) and label it `ui`.
3. A test that needs the internet or a swarm is labelled `network` and must be
   excluded by `-LE network`.

## Definition of done

- `cmake --build` clean and `ctest -LE network` green.
- The new behaviour has a test that fails without the change, unless it is
  purely cosmetic.
- User-visible behaviour is reflected in `README.md`; structure changes are
  reflected in `ARCHITECTURE.md`.
- One commit, one intent. Do not mix a refactor with a behaviour change, and do
  not reformat unrelated code.
- Update this file when a rule, a command or a module boundary changes.
