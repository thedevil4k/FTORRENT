#include "MainWindow.h"
#include "SearchManager.h"
#include "SearchScope.h"
#include "HttpClient.h"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu.H>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// Bullet prefix marking a reachability-checked engine row. U+25CF, in the same
// geometric-shapes block as the ▲▼ sort indicators the tables already draw,
// so any font rendering those renders this too.
const char* kEngineCheckedBullet = "\xE2\x97\x8F ";

} // namespace

void MainWindow::createSearchView() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int viewH = h() - y - STATUS_HEIGHT;
    int barY = y + 4;
    int barH = SEARCH_BAR_HEIGHT - 10;
    
    m_searchView = new Fl_Group(0, y, w(), viewH);
    m_searchView->box(FL_FLAT_BOX);
    m_searchView->begin();
    
    // --- Bar: engine chooser, category, query, buttons ---
    m_choiceSearchEngine = new Fl_Choice(10, barY, 150, barH);
    m_choiceSearchEngine->box(FL_FLAT_BOX);
    m_choiceSearchEngine->tooltip("Search engine (green = reachable, red = unreachable, gray = not checked yet)");
    m_choiceSearchEngine->callback([](Fl_Widget* w, void* data) {
        MainWindow* win = (MainWindow*)data;
        if (win) win->updateCategoryChoice();
    }, this);
    
    m_choiceSearchCategory = new Fl_Choice(165, barY, 95, barH);
    m_choiceSearchCategory->box(FL_FLAT_BOX);
    m_choiceSearchCategory->tooltip("Category (depends on the engine)");
    m_choiceSearchCategory->hide();   // only engines with categories show it
    
    m_searchInput = new Fl_Input(170, barY, 300, barH);
    m_searchInput->tooltip("Type what to look for and press Enter");
    m_searchInput->when(FL_WHEN_ENTER_KEY);
    m_searchInput->callback([](Fl_Widget* w, void* data) {
        MainWindow* win = (MainWindow*)data;
        if (win) win->startSearch();
    }, this);
    
    m_btnSearchGo = new Fl_Button(480, barY, 80, barH, "Search");
    m_btnSearchGo->callback(onSearchGo, this);
    
    // Cancel, in red because it aborts the running search
    m_btnSearchCancel = new Fl_Button(795, barY, 100, barH, "Cancel");
    m_btnSearchCancel->box(FL_DOWN_BOX);
    m_btnSearchCancel->labelsize(13);
    m_btnSearchCancel->labelcolor(FL_RED);
    m_btnSearchCancel->selection_color(fl_rgb_color(200, 60, 60));
    m_btnSearchCancel->color(fl_rgb_color(64, 20, 20));
    m_btnSearchCancel->tooltip("Stop the search in progress");
    m_btnSearchCancel->callback(onSearchCancelClick, this);
    m_btnSearchCancel->hide();   // only while searching
    
    // --- Status line, under the bar and above the results ---
    int stY = y + SEARCH_BAR_HEIGHT;
    m_searchStatus = new Fl_Box(10, stY, w() - 20, SEARCH_STATUS_HEIGHT,
                                "Type something and press Enter");
    m_searchStatus->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_searchStatus->labelsize(11);
    m_searchStatus->labelcolor(fl_rgb_color(120, 120, 120));
    
    // --- Results fill the rest ---
    int resY = stY + SEARCH_STATUS_HEIGHT;
    int resH = h() - resY - STATUS_HEIGHT;
    m_searchResults = new SearchResultsWidget(0, resY, w(), resH);
    // (SearchResultsWidget's constructor already restores the current group)
    
    m_searchView->end();
    
    // Results go straight to the add dialog, same as a dropped .torrent file.
    // Only leaves the search view when something was actually added, so a
    // cancelled dialog does not throw the results away (notably on Windows,
    // where the dialog is the whole download affordance). The row's file list
    // travels along so a multi-file magnet offers one checkbox per file.
    m_searchResults->setOnDownloadCallback(
        [this](const SearchResult& r, const std::vector<TorrentFileEntry>& files,
               const std::string& body) {
            if (showAddTorrentDialog("", r.magnet, files, r.torrentUrl, body)) {
                switchView(false);
            }
        });
    
    m_searchView->hide();   // Torrent list is what you see first
    
    layoutSearchViewContents(w());   // replace the hardcoded bar widths
}

void MainWindow::switchView(bool showSearch) {
    m_showSearchView = showSearch;
    
    if (m_searchView) {
        if (showSearch) m_searchView->show();
        else m_searchView->hide();
    }
    if (m_torrentList) {
        if (showSearch) m_torrentList->hide();
        else m_torrentList->show();
    }
    if (m_btnSearch) {
        m_btnSearch->tooltip(showSearch ? "Back to my torrents" : "Search torrents");
    }

    // First visit to the search view: probe every engine once, in the
    // background, so the dropdown dots reflect reality. Once per session.
    if (showSearch && !m_engineStatusChecked) startEngineStatusCheck();

    // Re-run the layout: one view appeared and the other disappeared
    layoutSearchView();
    redraw();
}

// Positions whichever view is visible in the rectangle below the toolbar.

void MainWindow::layoutSearchView() {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int listH = h() - y - STATUS_HEIGHT;
    if (listH < 40) listH = 40;
    int viewW = w();
    
    if (m_showSearchView) {
        if (m_searchView) m_searchView->resize(0, y, viewW, listH);
        layoutSearchViewContents(viewW);
    } else if (m_torrentList) {
        m_torrentList->resize(0, y, viewW, listH);
    }
}

void MainWindow::layoutSearchViewContents(int available) {
    int y = MENU_HEIGHT + TOOLBAR_HEIGHT;
    int barY = y + 4;
    int barH = SEARCH_BAR_HEIGHT - 10;
    
    // The query field takes whatever the fixed-width widgets leave, so the
    // bar adapts to the window instead of clipping the buttons.
    // Positions: 10 | engine | 5 | category? | 5 | input | 5 | Search
    const int wGo = 80, wCancel = 100;
    const int MARGIN_L = 10, MARGIN_R = 10, GAP = 5;
    int wEngine = (available < 820) ? 120 : 150;
    int wCat = (m_choiceSearchCategory && m_choiceSearchCategory->visible()) ? 95 : 0;
    int fixed = MARGIN_L + wEngine + GAP + wCat + (wCat > 0 ? GAP : 0)
              + GAP + wGo
              + GAP + wCancel + MARGIN_R;
    int wInput = available - fixed;
    if (wInput < 120) wInput = 120;
    
    m_choiceSearchEngine->resize(MARGIN_L, barY, wEngine, barH);
    int cx = MARGIN_L + wEngine + GAP;
    if (wCat > 0 && m_choiceSearchCategory) {
        m_choiceSearchCategory->resize(cx, barY, wCat, barH);
        cx += wCat + GAP;
    }
    m_searchInput->resize(cx, barY, wInput, barH);
    
    int bx = cx + wInput + GAP;
    m_btnSearchGo->resize(bx, barY, wGo, barH);
    bx += wGo + GAP;
    if (m_btnSearchCancel) m_btnSearchCancel->resize(bx, barY, wCancel, barH);
    
    // Status line spans the panel so long messages are not cut off
    int stY = y + SEARCH_BAR_HEIGHT;
    m_searchStatus->resize(MARGIN_L, stY, available - MARGIN_L - MARGIN_R, SEARCH_STATUS_HEIGHT);
    
    int resY = stY + SEARCH_STATUS_HEIGHT;
    int resH = h() - resY - STATUS_HEIGHT;
    if (resH < 40) resH = 40;
    
    if (m_searchResults) {
        m_searchResults->resize(0, resY, available, resH);
    }
}

// The toolbar has no layout manager that shrinks widgets for us: the pack lays
// every child out at the width it was constructed with, and if they add up to
// more than the window it grows past the window edge instead. So we ask
// ToolbarLayout which level of detail fits and apply it.
//
// The pack also stretches every child to its own height, which is why it is
// sized to kButtonHeight rather than to the bar height: the bar is the strip,
// the pack is the row of buttons inside it.
//
// The numbers live in ToolbarLayout; this function only moves widgets.

void MainWindow::setSearchStatus(const std::string& text) {
    if (m_searchStatus) {
        m_searchStatus->copy_label(text.c_str());
        m_searchStatus->redraw();
    }
}

// Rebuilds the engine dropdown from SearchManager

void MainWindow::updateEngineChoice() {
    if (!m_choiceSearchEngine) return;

    // Drop the menu first: rows below borrow text owned here, so the menu
    // must never outlive a rebuild of these strings, whatever copy semantics
    // a given FLTK release uses for add()/label().
    m_choiceSearchEngine->clear();
    m_engineLabels.clear();

    // Row 0 asks every enabled engine at once. Each one runs in its own worker
    // and fills the table as it answers, so a slow site never holds up a fast
    // one. Every other row is a single engine, as before.
    m_engineLabels.push_back("All engines");
    m_choiceSearchEngine->add(m_engineLabels.back().c_str());

    int index = 1;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        // Gray bullet until probed; a cached green/red survives rebuilds.
        // Plain text rows with a per-row color: menu-item images proved
        // unrenderable on some FLTK builds, while text draws everywhere.
        m_engineLabels.push_back(engineLabel(d.name));
        m_choiceSearchEngine->add(m_engineLabels.back().c_str());
        Fl_Menu_Item* item =
            const_cast<Fl_Menu_Item*>(m_choiceSearchEngine->menu() + index);
        item->labelcolor(engineColor(engineStatusOf(d.name)));
        index++;
    }
    // Default to the first real engine, not to the all-engines mode: a search
    // the user did not ask to widen must not reach every site at once. The
    // all-engines row stays available as an explicit choice.
    if (index > 1) m_choiceSearchEngine->value(1);
    m_choiceSearchEngine->redraw();
    updateCategoryChoice();
}

// Menu row of an engine in updateEngineChoice()'s order, which starts at 1
// because row 0 is "All engines". -1 when the engine is not listed.

int MainWindow::engineMenuIndex(const std::string& name) const {
    int index = 1;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        if (d.name == name) return index;
        index++;
    }
    return -1;
}

MainWindow::EngineStatus MainWindow::engineStatusOf(
    const std::string& name) const {
    auto it = m_engineStatus.find(name);
    if (it == m_engineStatus.end()) return EngineStatus::Unknown;
    return it->second;
}

Fl_Color MainWindow::engineColor(EngineStatus status) {
    // Gray reads on both themes; green/red carry the verdict.
    switch (status) {
        case EngineStatus::Up:   return fl_rgb_color(70, 190, 80);
        case EngineStatus::Down: return fl_rgb_color(220, 70, 70);
        default:                 return fl_rgb_color(150, 150, 150);
    }
}

std::string MainWindow::engineLabel(const std::string& name) {
    return std::string(kEngineCheckedBullet) + name;
}

// One joinable probe per enabled engine. Plain HttpClient GETs (the same door
// every search uses, no new libraries), short timeouts, and the window-dead
// check as the stop condition so closing the app aborts the probes instead of
// delivering into a freed window. Results hop to the UI thread via Fl::awake.
// Joinable rather than detached on purpose: a detached probe doing a blocking
// GET outlives main() and segfaults inside teardown, which is exactly what the
// toolbar_clicks suite caught. See shutdownEngineProbes().

void MainWindow::startEngineStatusCheck() {
    if (m_engineStatusChecked) return;   // once per session; also post-shutdown
    m_engineStatusChecked = true;
    for (const auto& d : SearchManager::instance().engines()) {
        if (!d.enabled) continue;
        SearchEngine::Definition def = d;   // the worker must not read the UI
        m_engineProbes.emplace_back([this, def] {
            if (!isAlive() || m_probesStop.load()) return;
            SearchEngine engine(def);
            HttpClient::Response resp = HttpClient::get(
                engine.buildUrl("test"), std::string("FTORRENT ") + VERSION,
                10,
                [this] {
                    return !MainWindow::isAlive() || m_probesStop.load();
                },
                nullptr, 6);
            if (!isAlive()) return;
            auto* delivery =
                new EngineStatusDelivery{this, def.name, resp.ok};
            Fl::awake(handleEngineStatus, delivery);
        });
    }
}

void MainWindow::shutdownEngineProbes() {
    // No new probes after this: a late switchView(true) must not resurrect
    // workers nobody will join. The flag aborts in-flight GETs at once so the
    // join below never waits out a full timeout.
    m_engineStatusChecked = true;
    m_probesStop.store(true);
    for (auto& t : m_engineProbes) {
        if (t.joinable()) t.join();
    }
    m_engineProbes.clear();
}

void MainWindow::handleEngineStatus(void* data) {
    std::unique_ptr<EngineStatusDelivery> delivery(
        static_cast<EngineStatusDelivery*>(data));
    if (!delivery || !isAlive() || !delivery->win) return;
    delivery->win->applyEngineStatus(delivery->name, delivery->up);
}

void MainWindow::applyEngineStatus(const std::string& name, bool up) {
    m_engineStatus[name] = up ? EngineStatus::Up : EngineStatus::Down;
    if (!m_choiceSearchEngine) return;
    int index = engineMenuIndex(name);
    if (index < 0) return;
    // Recolor in place: the text ("● Name", set at build time) is untouched,
    // so a status arrival can never blank a row.
    Fl_Menu_Item* item =
        const_cast<Fl_Menu_Item*>(m_choiceSearchEngine->menu() + index);
    item->labelcolor(engineColor(engineStatusOf(name)));
    m_choiceSearchEngine->redraw();
}

// Rebuilds the category dropdown from the selected engine. Engines with a
// single (or no) category hide the widget and always search "all".

void MainWindow::updateCategoryChoice() {
    if (!m_choiceSearchCategory) return;
    
    // Row 0 is "All engines", which has no categories of its own: searching
    // everything means "all" for each site.
    const SearchEngine::Definition* def = nullptr;
    if (m_choiceSearchEngine && m_choiceSearchEngine->value() > 0) {
        int row = 1;
        for (const auto& d : SearchManager::instance().engines()) {
            if (!d.enabled) continue;
            if (row++ == m_choiceSearchEngine->value()) {
                def = &d;
                break;
            }
        }
    }
    
    m_choiceSearchCategory->clear();
    if (!def || def->categories.size() < 2) {
        m_choiceSearchCategory->hide();
    } else {
        int allAt = -1;
        int index = 0;
        for (const auto& kv : def->categories) {
            m_choiceSearchCategory->add(kv.first.c_str());
            if (kv.first == "All" || kv.first == "all") allAt = index;
            index++;
        }
        m_choiceSearchCategory->value(allAt >= 0 ? allAt : 0);
        m_choiceSearchCategory->show();
    }
    m_choiceSearchCategory->redraw();
    if (m_searchView && m_searchView->visible()) {
        layoutSearchViewContents(w());
        redraw();
    }
}

void MainWindow::loadSearchEngines() {
    // Only predefined engines: the catalogue lives in the code, so there is
    // nothing to load, merge or save. Stale plugin files from older versions
    // are ignored on purpose.
    SearchManager::instance().clear();
    for (const auto& d : SearchEngine::builtinEngines()) {
        SearchManager::instance().addEngine(d);
    }
    updateEngineChoice();
}

// Runs the query on a worker thread and hands the results back to the UI
// thread through Fl::awake, since widgets may only be touched from there.

void MainWindow::startSearch() {
    if (m_searching) {
        // A new search replaces the previous one instead of piling up: the old
        // thread sees the flag and stops at its next checkpoint, and the new one
        // takes over the flag below. Rows already shown are cleared once the
        // previous thread hands the state back, so the two never interleave.
        m_searchCancelled = true;
        if (m_btnSearchCancel) m_btnSearchCancel->deactivate();
    }
    
    if (!m_choiceSearchEngine || m_choiceSearchEngine->value() < 0) {
        setSearchStatus("No search engine available");
        return;
    }
    
    std::string query = m_searchInput ? m_searchInput->value() : "";
    if (query.empty()) {
        setSearchStatus("Type something and press Enter");
        return;
    }
    
    // Row 0 is the explicit "All engines" mode; every other row picks exactly
    // one engine, and the requests go to that engine only. The mapping lives in
    // SearchScope so it can be tested.
    const int selection = m_choiceSearchEngine->value();
    std::vector<SearchEngine::Definition> engines =
        searchScopeForRow(SearchManager::instance().engines(), selection);
    if (engines.empty()) {
        setSearchStatus("No search engine available");
        return;
    }
    
    // Category of the selected engine, or "all" when it declares none. A
    // search across every engine has no single category to offer.
    std::string category = "all";
    if (!isAllEnginesRow(selection) && m_choiceSearchCategory &&
        m_choiceSearchCategory->visible() && m_choiceSearchCategory->value() >= 0) {
        const char* picked = m_choiceSearchCategory->text(m_choiceSearchCategory->value());
        if (picked && picked[0] != '\0') category = picked;
    }
    
    // Each search gets a number, so a thread that was superseded can tell that
    // it no longer owns the state and hand it back untouched.
    const unsigned long generation = ++m_searchGeneration;
    m_searching = true;
    m_searchCancelled = false;
    m_engineStates.clear();
    for (const SearchEngine::Definition& def : engines) {
        m_engineStates[def.name] = EngineState{};
    }
    if (m_searchResults) m_searchResults->clear();
    {
        // The generation was just bumped above, so no message from the search
        // being replaced can be parked after this point. Whatever it had
        // already parsed is dropped here: the table was just emptied, and
        // those rows belong to a search the user has moved on from.
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingEvents.clear();
    }
    if (m_btnSearchCancel) m_btnSearchCancel->show();
    if (m_btnSearchGo) m_btnSearchGo->deactivate();
    m_toolbar->redraw();
    updateSearchProgressStatus();

    // One thread per engine, not one per search. A site that hangs costs its
    // own row in the status line and nothing else: the others keep filling the
    // table while it times out.
    for (const SearchEngine::Definition& def : engines) {
        std::thread([this, query, def, generation, category]() {
            SearchEngine engine(def);

            auto isCurrent = [this, generation]() {
                return generation == m_searchGeneration.load();
            };

            // Rows are delivered as they are parsed rather than all at the end,
            // so a slow site fills the table progressively. Fl::awake only takes
            // a plain function, so each message is parked under the mutex and
            // picked up there.
            std::string error;
            bool ok = false;
            try {
                ok = engine.searchStreaming(query,
                    [this, def, generation, isCurrent](
                        const std::vector<SearchResult>& batch) {
                        if (batch.empty() || !isCurrent()) return;
                        SearchEvent event;
                        event.kind = SearchEvent::Kind::Batch;
                        event.engine = def.name;
                        event.batch = batch;
                        postSearchEvent(std::move(event), generation);
                    },
                    [this, isCurrent]() {
                        return !isCurrent() || m_searchCancelled.load();
                    },
                    error,
                    category,
                    [this, def, generation, isCurrent](const std::string& stage) {
                        if (stage.empty() || !isCurrent()) return;
                        SearchEvent event;
                        event.kind = SearchEvent::Kind::Stage;
                        event.engine = def.name;
                        event.text = stage;
                        postSearchEvent(std::move(event), generation);
                    });
            } catch (const std::exception& e) {
                ok = false;
                error = e.what();
            } catch (...) {
                ok = false;
                error = "the engine failed unexpectedly";
            }

            // A newer search took over: leave the state, the table and the
            // buttons to it.
            if (!isCurrent()) return;

            SearchEvent done;
            done.kind = SearchEvent::Kind::EngineDone;
            done.engine = def.name;
            done.text = error;
            done.ok = ok;
            postSearchEvent(std::move(done), generation);
        }).detach();
    }
}

// Worker side. The generation is checked again with the lock held: a new search
// may have taken over between the caller's test and this line, and its table
// was cleared. Parking a message from the superseded search now would mix its
// rows into the new results.

void MainWindow::postSearchEvent(SearchEvent event, unsigned long generation) {
    if (!s_alive) return;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (generation != m_searchGeneration.load()) return;
        event.generation = generation;
        m_pendingEvents.push_back(std::move(event));
    }
    Fl::awake(&MainWindow::handleSearchEvents, this);
}

// UI side: drains everything the workers posted and repaints once.

void MainWindow::handleSearchEvents(void* data) {
    MainWindow* win = static_cast<MainWindow*>(data);
    if (!win || !s_alive) return;
    // A cancelled search put the UI back in order already; nothing it parked
    // may be painted now.
    if (!win->m_searching) return;

    const unsigned long generation = win->m_searchGeneration.load();
    std::deque<SearchEvent> events;
    {
        std::lock_guard<std::mutex> lock(win->m_searchMutex);
        events.swap(win->m_pendingEvents);
    }
    if (events.empty()) return;

    // Every batch of this wake is appended together: one re-sort and one
    // repaint, however many engines and batches are behind it.
    std::vector<SearchResult> rows;
    bool finishedOne = false;
    std::string lastStage;
    std::string lastError;
    bool errorFromSingle = false;

    for (SearchEvent& event : events) {
        // Parked by a search that has since been replaced or cancelled.
        if (event.generation != generation) continue;
        switch (event.kind) {
            case SearchEvent::Kind::Batch:
                rows.insert(rows.end(), event.batch.begin(), event.batch.end());
                break;
            case SearchEvent::Kind::Stage:
                lastStage = event.text;
                break;
            case SearchEvent::Kind::EngineDone: {
                auto state = win->m_engineStates.find(event.engine);
                if (state != win->m_engineStates.end()) {
                    state->second.running = false;
                    state->second.failed = !event.ok;
                }
                if (!event.ok && !event.text.empty()) {
                    lastError = event.text;
                    errorFromSingle = win->m_engineStates.size() == 1;
                }
                finishedOne = true;
                break;
            }
        }
    }

    if (!rows.empty() && win->m_searchResults) {
        win->m_searchResults->appendResults(rows);
    }

    if (!win->m_searching) return;   // cancelled while this was in flight

    if (win->m_engineStates.size() <= 1) {
        // One engine: its own stage line is the most useful thing to show.
        if (errorFromSingle && !lastError.empty()) {
            win->setSearchStatus(lastError);
        } else if (!lastStage.empty()) {
            win->setSearchStatus(lastStage);
        } else if (!rows.empty() && win->m_searchResults) {
            win->setSearchStatus(
                std::to_string(win->m_searchResults->rowCount()) +
                " results...");
        }
    }

    if (win->m_engineStates.size() > 1 && finishedOne) {
        win->updateSearchProgressStatus();
    }

    bool stillRunning = false;
    for (const auto& entry : win->m_engineStates) {
        if (entry.second.running) stillRunning = true;
    }
    if (!stillRunning) win->finishSearch();
}

// UI side: one line for a whole search, so several engines can report at once
// without fighting over the status widget.

void MainWindow::updateSearchProgressStatus() {
    const int total = static_cast<int>(m_engineStates.size());
    const int rows = m_searchResults ? m_searchResults->rowCount() : 0;

    if (total <= 1) {
        setSearchStatus(rows > 0 ? std::to_string(rows) + " results..."
                                 : std::string("Searching..."));
        return;
    }

    int done = 0;
    std::string failedNames;
    for (const auto& entry : m_engineStates) {
        if (!entry.second.running) ++done;
        if (entry.second.failed) {
            if (!failedNames.empty()) failedNames += ", ";
            failedNames += entry.first;
        }
    }

    std::string text = "Searching " + std::to_string(done) + "/" +
                       std::to_string(total) + " engines";
    if (rows > 0) text += " · " + std::to_string(rows) + " results";
    if (!failedNames.empty()) text += " · failed: " + failedNames;
    setSearchStatus(text);
}

// UI side: the last engine of a search has reported.

void MainWindow::finishSearch() {
    m_searching = false;
    m_searchCancelled = false;
    if (m_btnSearchCancel) m_btnSearchCancel->hide();
    if (m_btnSearchGo) m_btnSearchGo->activate();
    m_toolbar->redraw();

    const int rows = m_searchResults ? m_searchResults->rowCount() : 0;
    int answered = 0;
    std::string failedNames;
    for (const auto& entry : m_engineStates) {
        if (!entry.second.failed) ++answered;
        else {
            if (!failedNames.empty()) failedNames += ", ";
            failedNames += entry.first;
        }
    }

    std::string text;
    if (rows == 0) {
        text = "No results";
    } else {
        text = std::to_string(rows) + (rows == 1 ? " result" : " results");
        if (m_engineStates.size() > 1) {
            text += " from " + std::to_string(answered) + " engine(s)";
        }
    }
    if (!failedNames.empty()) text += " · failed: " + failedNames;
    setSearchStatus(text);
}

void MainWindow::onSearchCancelClick(Fl_Widget* w, void* data) {
    MainWindow* win = (MainWindow*)data;
    if (!win || !win->m_searching) return;
    
    // The flag alone is enough to stop an in-flight request: it is checked
    // several times per second by the transfer, so the worker unwinds on its
    // own and the UI is put back in order right away instead of after it.
    win->m_searchCancelled = true;
    win->m_searching = false;
    win->setSearchStatus("Search cancelled");
    
    if (win->m_btnSearchCancel) win->m_btnSearchCancel->hide();
    if (win->m_btnSearchGo) win->m_btnSearchGo->activate();
    if (win->m_searchResults) {
        int found = win->m_searchResults->rowCount();
        if (found > 0) {
            win->setSearchStatus("Cancelled with " + std::to_string(found) +
                                 " results so far");
        }
    }
    
    // Bumping the generation tells every worker that its results no longer
    // belong on screen, so nothing lands in the table after the cancel. The
    // queue is emptied too: whatever was already parked belongs to the search
    // the user just stopped.
    ++win->m_searchGeneration;
    {
        std::lock_guard<std::mutex> lock(win->m_searchMutex);
        win->m_pendingEvents.clear();
    }
    win->m_engineStates.clear();
}

// Repaints the status bar after the latency measurement finishes.
