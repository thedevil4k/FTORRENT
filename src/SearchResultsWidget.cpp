#include "SearchResultsWidget.h"
#include <FL/fl_draw.H>
#include "TorrentItem.h"   // formatSize, used for the size column

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

const int BASE_ROW_HEIGHT = 25;      // matches initializeColumns()
const int FILE_LINE_HEIGHT = 16;
// Enough to see what is inside without turning a row into a wall of text.
const int MAX_SHOWN_FILES = 12;
std::string elide(const std::string& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    return text.substr(0, maxChars - 3) + "...";
}

} // namespace

const SearchResultsWidget::ColumnInfo SearchResultsWidget::COLUMN_INFO[COL_COUNT] = {
    {"",         24, FL_ALIGN_CENTER},
    {"Name",     400, FL_ALIGN_LEFT},
    {"Size",     110, FL_ALIGN_RIGHT},
    {"Seeders",   90, FL_ALIGN_RIGHT},
    {"Leechers",  90, FL_ALIGN_RIGHT},
    {"Source",   120, FL_ALIGN_LEFT}
};

SearchResultsWidget::SearchResultsWidget(int x, int y, int w, int h, const char* label)
    : Fl_Table_Row(x, y, w, h, label)
    , m_sortColumn(COL_SEEDERS)
    , m_sortAscending(true)
    , m_headerBg(FL_LIGHT2)
    , m_oddRowColor(fl_rgb_color(248, 248, 248))
{
    initializeColumns();
    
    type(SELECT_SINGLE);
    when(FL_WHEN_RELEASE);
    
    scrollbar_size(12);
    vscrollbar->box(FL_FLAT_BOX);
    hscrollbar->box(FL_FLAT_BOX);
    
    end();
}

SearchResultsWidget::~SearchResultsWidget() = default;

std::string SearchResultsWidget::cacheKey(const SearchResult& r) {
    // The infohash is the one identifier two engines agree on, so the same
    // torrent reached from BitSearch and from SolidTorrents is fetched once.
    std::string hash = TorrentFileList::magnetHash(r.magnet);
    if (!hash.empty()) return hash;
    if (!r.torrentUrl.empty()) return "url:" + r.torrentUrl;
    return "row:" + r.name + ":" + std::to_string(r.size);
}

FileListService::Entry SearchResultsWidget::filesForRow(int row) const {
    const SearchResult* r = getResultAt(row);
    return r ? m_files.find(cacheKey(*r)) : FileListService::Entry{};
}

bool SearchResultsWidget::isExpanded(const std::string& key) const {
    auto it = m_expanded.find(key);
    return it != m_expanded.end() && it->second;
}

void SearchResultsWidget::setExpanded(const std::string& key, bool on) {
    if (!on) m_expanded.erase(key);
    else     m_expanded[key] = true;
}

bool SearchResultsWidget::isRowExpanded(int row) const {
    const SearchResult* r = getResultAt(row);
    return r && isExpanded(cacheKey(*r));
}

void SearchResultsWidget::forEachRow(const std::function<void(int, const SearchResult&)>& fn) {
    for (int row = 0; row < rows(); ++row) {
        const SearchResult* r = getResultAt(row);
        if (r) fn(row, *r);
    }
}

void SearchResultsWidget::requestFiles(int row) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;
    SearchResult copy = *r;                 // the fetch must not read the table
    std::string key = cacheKey(copy);

    if (m_files.request(key, copy.magnet, copy.torrentUrl,
                        [this, key](const FileListService::Entry&) { filesArrived(key); })) {
        // The row now has a "reading it" line under the name; make room for it.
        updateRowHeight(row);
        redraw();
    }
}

// A fetch for 'key' landed. Only the rows showing that same torrent change --
// the same torrent can sit on several rows, and nothing else has moved.
void SearchResultsWidget::filesArrived(const std::string& key) {
    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
}

void SearchResultsWidget::toggleExpand(int row) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;
    std::string key = cacheKey(*r);
    bool nowExpanded = !isExpanded(key);
    setExpanded(key, nowExpanded);
    updateRowHeight(row);
    redraw();
    if (nowExpanded) {
        // Starts the fetch and returns at once; the row redraws on its own
        // when the answer lands.
        requestFiles(row);
    }
}

// ---------------------------------------------------------------------------
// Columns

void SearchResultsWidget::initializeColumns() {
    cols(COL_COUNT);
    col_header(1);
    col_resize(1);
    
    for (int i = 0; i < COL_COUNT; i++) {
        col_width(i, COLUMN_INFO[i].width);
    }
    
    row_height_all(BASE_ROW_HEIGHT);
    row_header(0);
    
    layoutColumns();
}

void SearchResultsWidget::layoutColumns() {
    int available = w();
    if (available <= 0) return;
    
    int others = 0;
    for (int i = 0; i < COL_COUNT; i++) {
        if (i != COL_NAME) others += COLUMN_INFO[i].width;
    }
    col_width(COL_NAME, available - others);
}

void SearchResultsWidget::resize(int X, int Y, int W, int H) {
    Fl_Table_Row::resize(X, Y, W, H);
    layoutColumns();
}

void SearchResultsWidget::setResults(const std::vector<SearchResult>& results) {
    m_results = results;
    updateSortedIndices();
    rows(m_results.size());
    row_height_all(BASE_ROW_HEIGHT);
    redraw();
}

void SearchResultsWidget::appendResults(const std::vector<SearchResult>& results) {
    if (results.empty()) return;
    
    m_results.insert(m_results.end(), results.begin(), results.end());
    updateSortedIndices();
    rows(m_results.size());
    // Do not scroll: the user may be reading an earlier row.
    redraw();
}

void SearchResultsWidget::clear() {
    m_results.clear();
    m_sortedIndices.clear();
    m_files.clear();
    m_expanded.clear();
    rows(0);
    row_height_all(BASE_ROW_HEIGHT);
    redraw();
}

const SearchResult* SearchResultsWidget::getResultAt(int row) const {
    if (row < 0 || row >= (int)m_sortedIndices.size()) return nullptr;
    int idx = m_sortedIndices[row];
    if (idx < 0 || idx >= (int)m_results.size()) return nullptr;
    return &m_results[idx];
}

int SearchResultsWidget::selectedRow() {
    for (int r = 0; r < rows(); r++) {
        if (is_selected(r, 0)) return r;
    }
    return -1;
}

void SearchResultsWidget::applyTheme(bool darkMode) {
    if (darkMode) {
        m_headerBg = fl_rgb_color(55, 55, 55);
        m_oddRowColor = fl_rgb_color(38, 38, 38);
    } else {
        m_headerBg = FL_LIGHT2;
        m_oddRowColor = fl_rgb_color(248, 248, 248);
    }
    
    Fl_Color trough = darkMode ? fl_rgb_color(45, 45, 45) : fl_rgb_color(232, 232, 232);
    Fl_Color knob = darkMode ? fl_rgb_color(120, 120, 120) : fl_rgb_color(150, 150, 150);
    if (vscrollbar) {
        vscrollbar->color(trough);
        vscrollbar->selection_color(knob);
        vscrollbar->redraw();
    }
    if (hscrollbar) {
        hscrollbar->color(trough);
        hscrollbar->selection_color(knob);
        hscrollbar->redraw();
    }
    redraw();
}

void SearchResultsWidget::draw() {
    Fl_Table_Row::draw();
}

void SearchResultsWidget::drawHeader(int col, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x, y, w, h);
    fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, m_headerBg);
    
    if (col >= 0 && col < COL_COUNT) {
        fl_color(FL_FOREGROUND_COLOR);
        fl_font(FL_HELVETICA_BOLD, 12);
        std::string label = COLUMN_INFO[col].name;
        if (col == m_sortColumn) label += m_sortAscending ? " ▲" : " ▼";
        fl_draw(label.c_str(), x + 5, y, w - 10, h, COLUMN_INFO[col].alignment);
    }
    
    fl_pop_clip();
}

void SearchResultsWidget::updateRowHeight(int row) {
    if (row < 0 || row >= rows() || row >= (int)m_sortedIndices.size()) {
        row_height_all(BASE_ROW_HEIGHT);
        return;
    }
    const SearchResult* r = getResultAt(row);
    if (!r || !isExpanded(cacheKey(*r))) {
        row_height(row, BASE_ROW_HEIGHT);
        return;
    }

    FileListService::Entry rf = filesForRow(row);
    int lines = 0;
    switch (rf.state) {
        case FileListState::Loaded:
            lines = static_cast<int>(rf.files.size());
            if (lines > MAX_SHOWN_FILES) lines = MAX_SHOWN_FILES + 1;   // + "and N more"
            break;
        case FileListState::Loading:
        case FileListState::Failed:
            lines = 1;
            break;
        default:
            lines = 0;
            break;
    }
    row_height(row, BASE_ROW_HEIGHT + lines * FILE_LINE_HEIGHT);
}

// The little triangle that opens the file list, plus a hint of the state.
void SearchResultsWidget::drawExpandCell(int row, int x, int y, int w, int h) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;

    bool expanded = isExpanded(cacheKey(*r));
    FileListService::Entry rf = filesForRow(row);

    Fl_Color fg = FL_FOREGROUND_COLOR;
    if (rf.state == FileListState::Loading) fg = fl_rgb_color(140, 140, 140);

    // Centre the triangle in the top half of the cell; a row that is open is
    // taller and the triangle must stay next to the name, not in the middle.
    int cx = x + w / 2;
    int cy = y + BASE_ROW_HEIGHT / 2;
    int s = 4;

    // Three lines rather than fl_polygon(): the shape stays the same on every
    // FLTK backend, including the ones that rasterise fills differently.
    fl_color(fg);
    if (expanded) {           // pointing down
        fl_line(cx - s, cy - s / 2, cx + s, cy - s / 2);
        fl_line(cx + s, cy - s / 2, cx, cy + s);
        fl_line(cx, cy + s, cx - s, cy - s / 2);
    } else {                  // pointing right
        fl_line(cx - s / 2, cy - s, cx - s / 2, cy + s);
        fl_line(cx - s / 2, cy + s, cx + s, cy);
        fl_line(cx + s, cy, cx - s / 2, cy - s);
    }
}

// Draws the files under the name. Deliberately plain text: the user is
// checking extensions, so names must not be interpreted as markup.
void SearchResultsWidget::drawFileList(int row, int x, int y, int w, int h) {
    const SearchResult* r = getResultAt(row);
    if (!r || !isExpanded(cacheKey(*r))) return;

    FileListService::Entry rf = filesForRow(row);
    fl_font(FL_HELVETICA, 11);

    int ly = y + BASE_ROW_HEIGHT;
    int limit = y + h;

    if (rf.state == FileListState::Loading) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_draw("Reading the file list...", x + 24, ly, w - 26, FILE_LINE_HEIGHT, FL_ALIGN_LEFT);
        return;
    }
    if (rf.state == FileListState::Failed) {
        std::string msg = rf.error.empty() ? "The file list could not be obtained"
                                           : ("File list unavailable: " + rf.error);
        fl_draw(elide(msg, 90).c_str(), x + 24, ly, w - 26, FILE_LINE_HEIGHT, FL_ALIGN_LEFT);
        return;
    }
    if (rf.state == FileListState::NotRequested) return;

    int shown = static_cast<int>(rf.files.size());
    if (shown > MAX_SHOWN_FILES) shown = MAX_SHOWN_FILES;
    for (int i = 0; i < shown && ly < limit; ++i) {
        const TorrentFileEntry& f = rf.files[static_cast<size_t>(i)];
        fl_color(FL_FOREGROUND_COLOR);
        std::string label = elide(f.path, 90) + "  (" + TorrentItem::formatSize(f.size) + ")";
        fl_draw(label.c_str(), x + 24, ly, w - 26, FILE_LINE_HEIGHT, FL_ALIGN_LEFT);
        ly += FILE_LINE_HEIGHT;
    }
    if (static_cast<int>(rf.files.size()) > MAX_SHOWN_FILES && ly < limit) {
        fl_color(fl_rgb_color(140, 140, 140));
        std::string more = "... and " +
            std::to_string(static_cast<int>(rf.files.size()) - MAX_SHOWN_FILES) + " more";
        fl_draw(more.c_str(), x + 24, ly, w - 26, FILE_LINE_HEIGHT, FL_ALIGN_LEFT);
    }
}

void SearchResultsWidget::drawCell(int row, int col, int x, int y, int w, int h) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;
    
    fl_push_clip(x, y, w, h);
    
    fl_color(row % 2 ? m_oddRowColor : FL_BACKGROUND2_COLOR);
    fl_rectf(x, y, w, h);
    
    fl_color(FL_FOREGROUND_COLOR);
    fl_font(FL_HELVETICA, 12);
    
    if (col == COL_EXPAND) {
        drawExpandCell(row, x, y, w, h);
        fl_pop_clip();
        return;
    }
    
    std::string text;
    switch (col) {
        case COL_NAME:     text = r->name; break;
        case COL_SIZE:     text = r->size > 0 ? TorrentItem::formatSize(r->size) : "?"; break;
        case COL_SEEDERS:  text = r->seeders >= 0 ? std::to_string(r->seeders) : "?"; break;
        case COL_LEECHERS: text = r->leechers >= 0 ? std::to_string(r->leechers) : "?"; break;
        case COL_SOURCE:   text = r->source; break;
        default: break;
    }
    
    if (text.size() > 60) text = text.substr(0, 57) + "...";
    fl_draw(text.c_str(), x + 5, y, w - 10, h, COLUMN_INFO[col].alignment);

    if (col == COL_NAME) drawFileList(row, x, y, w, h);
    
    fl_pop_clip();
}

bool SearchResultsWidget::compareResults(int idx1, int idx2) const {
    const SearchResult& a = m_results[idx1];
    const SearchResult& b = m_results[idx2];
    
    int result = 0;
    switch (m_sortColumn) {
        case COL_NAME:
            result = a.name.compare(b.name);
            break;
        case COL_SIZE:
            result = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case COL_SEEDERS:
            result = (a.seeders < b.seeders) ? -1 : (a.seeders > b.seeders ? 1 : 0);
            break;
        case COL_LEECHERS:
            result = (a.leechers < b.leechers) ? -1 : (a.leechers > b.leechers ? 1 : 0);
            break;
        case COL_SOURCE:
            result = a.source.compare(b.source);
            break;
        default:
            break;
    }
    return m_sortAscending ? (result < 0) : (result > 0);
}

void SearchResultsWidget::updateSortedIndices() {
    m_sortedIndices.resize(m_results.size());
    for (size_t i = 0; i < m_results.size(); ++i) m_sortedIndices[i] = (int)i;
    std::stable_sort(m_sortedIndices.begin(), m_sortedIndices.end(),
                     [this](int a, int b) { return compareResults(a, b); });
}

void SearchResultsWidget::draw_cell(TableContext context, int row, int col,
                                    int x, int y, int w, int h) {
    switch (context) {
        case CONTEXT_COL_HEADER:
            drawHeader(col, x, y, w, h);
            break;
        case CONTEXT_CELL:
            drawCell(row, col, x, y, w, h);
            break;
        default:
            break;
    }
}

int SearchResultsWidget::handle(int event) {
    if (event == FL_MOUSEWHEEL) {
        int dy = Fl::event_dy();
        int dx = Fl::event_dx();
        if (dy != 0) {
            row_position(top_row() + 3 * dy);
        } else if (dx != 0) {
            col_position(leftcol + dx);
        }
        redraw();
        return 1;
    }
    
    int result = Fl_Table_Row::handle(event);
    
    // Click on a column header: sort by it
    if (event == FL_RELEASE && callback_context() == CONTEXT_COL_HEADER) {
        if (Fl::event_is_click()) {
            int col = callback_col();
            // The toggle column has no text, so sorting by it is meaningless.
            if (col > COL_EXPAND && col < COL_COUNT) {
                if ((Column)col == m_sortColumn) {
                    m_sortAscending = !m_sortAscending;
                } else {
                    m_sortColumn = (Column)col;
                    m_sortAscending = (col != COL_SEEDERS);
                }
                updateSortedIndices();
                // Expansion follows the torrent, not the row, but the heights
                // are stored per row and the rows have just been permuted.
                forEachRow([this](int row, const SearchResult&) { updateRowHeight(row); });
                redraw();
                return 1;
            }
        }
    }
    
    // Click on the toggle: open or close the file list. Handled before the
    // download shortcut so a double click on the arrow never starts a
    // download the user did not ask for.
    if (event == FL_PUSH && callback_context() == CONTEXT_CELL &&
        callback_col() == COL_EXPAND) {
        if (Fl::event_clicks() == 0) {
            toggleExpand(callback_row());
        }
        return 1;
    }
    
    // Double-click: download the selected result
    if (event == FL_PUSH && Fl::event_clicks() > 0) {
        if (callback_context() == CONTEXT_CELL) {
            const SearchResult* r = getResultAt(selectedRow());
            if (r && m_onDownload) {
                m_onDownload(*r);
                return 1;
            }
        }
    }
    
    return result;
}