# Search feature: module structure

The layer diagram, the state-ownership table and the thread rule. Everything
else here is already written on the headers of the files it concerns; this note
exists for the facts you cannot read off any one file.

```
MainWindow ──┬─ SearchManager     engine catalogue
             ├─ SearchEngine      engine logic, no UI
             ├─ HttpClient        the one HTTP door
             ├─ TorrentFileList   what files does this torrent download?
             ├─ FileListService   owns the file-list cache and the fetches
             └─ SearchResultsWidget  draws the table, nothing else
```

The rule the arrows encode: **data flows down, never up.** A widget may call a
service; a service may not know a widget exists.

## Who owns what

| State | Owner |
|---|---|
| What is known about each torrent's files | `FileListService` — cache, not view |
| How many fetches run at once, and their lifetime | `FileListService` — same lifetime as the fetches |
| Which rows are expanded, which column is sorted | `SearchResultsWidget` — view state, UI thread only |
| Engine definitions | `SearchManager` |
| The mark on the magnet field | `AddTorrentDialog` — the only place the route is known |

`FileListService` is keyed by an **opaque string** the caller chooses. It knows
nothing about `SearchResult`, rows or engines, so it would back any other
surface that wants to show a torrent's contents.

## The thread rule

**Never write to a widget, or to anything the UI thread owns, from a worker.**
`FileListService::request()` is the only place in the feature that starts a
thread. It captures a `shared_ptr` to a shared block, never the service, so a
fetch can outlive its owner without reaching through it; results come back
through `Fl::awake()` and are dropped if the owner is gone. The in-flight
counter is released by `deliver()` on the UI thread for exactly this reason —
releasing it from the worker was a heap-use-after-free that AddressSanitizer
caught (`h7`).

## Not done, on purpose

`SearchResultsWidget` and `TorrentListWidget` still carry two copies of the same
table machinery (`layoutColumns`, `drawHeader`, `drawCell`, sorting, theming —
11 identically named methods). Factoring that into a shared base is the obvious
next move, but it restructures a widget this pass was not asked to touch.

## Harnesses

In `/tmp/harness`, linked against the real CMake objects, not in the repository:
`h3` every engine · `h4` file lists and the expand arrow · `h6` Pirate Face ·
`h7` ASan, table destroyed mid-fetch · `h8` expand a real result · `h9` the
green dot.