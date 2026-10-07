#include "SearchResultsWidget.h"
#include <FL/Fl.H>
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
// Minimum pixel width the Name column is allowed to shrink to. Keeps the
// table usable at the 720px minimum window width and avoids a zero/negative
// col_width() when the window is at its smallest.
const int MIN_NAME_WIDTH = 120;

// Truncates back to the previous UTF-8 character boundary so an elided name
// never ends in the middle of a multibyte sequence. Pure byte arithmetic,
// no locale or extra dependency, identical on every platform we ship.
size_t utf8Clip(const std::string& text, size_t pos) {
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

// Ellipsis by pixels, not by characters: with a proportional font 60 chars
// can be far narrower (or wider) than the cell. The caller must have set the
// same fl_font() it will draw with, since fl_width() measures that font.
// Returns the full text when it fits, so a maximised window shows the
// absolute name, and only adds "..." when the cell is really too narrow
// (square window). Uses only fl_width(), available in FLTK 1.3 and 1.4 on
// every backend we build (Windows x86/ARM64, Linux 32/64, Arch, Flatpak).
std::string elideToWidth(const std::string& text, int maxPixels) {
    if (text.empty() || maxPixels <= 0) return text.empty() ? text : std::string();
    if (fl_width(text.c_str()) <= maxPixels) return text;
    const char* ellipsis = "...";
    const int ellipsisW = static_cast<int>(fl_width(ellipsis));
    if (maxPixels <= ellipsisW) return std::string();
    size_t lo = 0;
    size_t hi = text.size();
    // Binary search on bytes, then snap to a UTF-8 boundary.
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        size_t cut = utf8Clip(text, mid);
        // A cut that lands inside a multibyte char maps several mids to the
        // same boundary: force progress so the loop always terminates.
        if (cut <= lo) break;
        std::string cand = text.substr(0, cut) + ellipsis;
        if (fl_width(cand.c_str()) <= maxPixels) lo = cut;
        else hi = cut - 1;
    }
    size_t finalCut = utf8Clip(text, lo);
    if (finalCut == 0) return ellipsis;
    return text.substr(0, finalCut) + ellipsis;
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
    // Descending: the most seeded (usable) torrents come first. Zero-seeder
    // rows are useless for downloading, so they belong at the bottom. This
    // matches the header-click rule below, where Seeders also defaults to
    // descending, and the header indicator shows its ▼ from the start.
    , m_sortAscending(false)
    , m_headerBg(FL_LIGHT2)
    , m_oddRowColor(fl_rgb_color(248, 248, 248))
{
    initializeColumns();
    
    type(SELECT_SINGLE);
    when(FL_WHEN_RELEASE);
    
    // See TorrentListWidget: the per-widget scrollbar_size() needs the
    // extended FLTK 1.3 ABI, so fall back to the global setter there.
#if defined(FLTK_ABI_VERSION) && FLTK_ABI_VERSION >= 10301
    scrollbar_size(12);
#else
    Fl::scrollbar_size(12);
#endif
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
    // Name takes whatever is left, but never collapses: at the 720px minimum
    // window this keeps every column reachable instead of going zero/negative.
    int nameW = available - others;
    if (nameW < MIN_NAME_WIDTH) nameW = MIN_NAME_WIDTH;
    col_width(COL_NAME, nameW);
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
    int avail = w - 26;
    if (avail <= 0) return;

    // One-pixel separator between file lines so a torrent with several files
    // reads as distinct rows instead of a block of text. Derived from the
    // live background colour, so it stays subtle in both themes without
    // storing any extra theme state.
    uchar br = 255, bg = 255, bb = 255;
    Fl::get_color(FL_BACKGROUND2_COLOR, br, bg, bb);
    const bool darkBg = br < 128;
    const Fl_Color sepColor =
        darkBg ? fl_rgb_color(70, 70, 70) : fl_rgb_color(210, 210, 210);

    if (rf.state == FileListState::Loading) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_draw("Reading the file list...", x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        return;
    }
    if (rf.state == FileListState::Failed) {
        std::string msg = rf.error.empty() ? "The file list could not be obtained"
                                           : ("File list unavailable: " + rf.error);
        msg = elideToWidth(msg, avail);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw(msg.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        return;
    }
    if (rf.state == FileListState::NotRequested) return;

    int shown = static_cast<int>(rf.files.size());
    if (shown > MAX_SHOWN_FILES) shown = MAX_SHOWN_FILES;
    for (int i = 0; i < shown && ly + FILE_LINE_HEIGHT <= limit + 1; ++i) {
        const TorrentFileEntry& f = rf.files[static_cast<size_t>(i)];
        // The size suffix is never cut: only the path is elided, measured
        // against what is left after the "- " bullet and the suffix itself.
        // Full path shows when the window is wide; "..." only when narrow.
        fl_font(FL_HELVETICA, 11);
        const std::string suffix = "  (" + TorrentItem::formatSize(f.size) + ")";
        const int bulletW = static_cast<int>(fl_width("- "));
        const int suffixW = static_cast<int>(fl_width(suffix.c_str()));
        std::string path = elideToWidth(f.path, avail - bulletW - suffixW);
        const std::string label = "- " + path + suffix;
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw(label.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        // Separator under every file line: makes each file its own row.
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_color(sepColor);
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2, ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
    }
    if (static_cast<int>(rf.files.size()) > MAX_SHOWN_FILES && ly < limit) {
        fl_color(fl_rgb_color(140, 140, 140));
        fl_font(FL_HELVETICA, 11);
        std::string more = "... and " +
            std::to_string(static_cast<int>(rf.files.size()) - MAX_SHOWN_FILES) + " more";
        more = elideToWidth(more, avail);
        fl_draw(more.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
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

    // Pixel-measured ellipsis: full absolute name when the maximised Name
    // column has room, "..." only when it really overflows (square window).
    // FL_ALIGN_CLIP is the second net, same as TorrentListWidget uses.
    int textAvail = w - 10;
    if (textAvail > 0 && !text.empty()) text = elideToWidth(text, textAvail);
    // The main value always lives in the top BASE_ROW_HEIGHT slice so an
    // expanded (taller) row keeps name/size/seeders aligned with the toggle
    // triangle instead of sinking to the vertical centre. The file list owns
    // everything below that slice.
    const int topH = h < BASE_ROW_HEIGHT ? h : BASE_ROW_HEIGHT;
    const Fl_Align align =
        static_cast<Fl_Align>(COLUMN_INFO[col].alignment | FL_ALIGN_CLIP);
    int tw = w - 10;
    if (tw < 0) tw = 0;
    fl_draw(text.c_str(), x + 5, y, tw, topH, align);

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
    
    // Double-click: download the clicked result.
    // Uses callback_row(), not selectedRow(): on Windows the second PUSH of a
    // double-click can arrive before Fl_Table_Row moves the selection, so
    // selectedRow() still points at the old row (or -1 when nothing was
    // selected) and the dialog never opened. callback_row() is the cell that
    // was actually hit, on every platform.
    if (event == FL_PUSH && Fl::event_clicks() > 0) {
        if (callback_context() == CONTEXT_CELL) {
            int clicked = callback_row();
            if (clicked < 0 || clicked >= rows()) clicked = selectedRow();
            const SearchResult* r = getResultAt(clicked);
            if (r && m_onDownload) {
                select_row(clicked, 1);
                SearchResult copy = *r;  // modal dialog spins Fl::wait()
                // Whatever this row already resolved (empty if never expanded
                // or failed): same order the dialog and libtorrent use.
                FileListService::Entry rf = filesForRow(clicked);
                std::vector<TorrentFileEntry> files;
                if (rf.state == FileListState::Loaded) files = rf.files;
                m_onDownload(copy, files);
                return 1;
            }
        }
    }

    // Enter key: same download, for users whose double-click speed setting
    // turns a double-click into two single clicks (then event_clicks() stays
    // 0 and the block above never fires, notably on Windows).
    if (event == FL_KEYBOARD) {
        int key = Fl::event_key();
        if (key == FL_Enter || key == FL_KP_Enter) {
            int sel = selectedRow();
            if (sel < 0 && rows() > 0) sel = 0;
            const SearchResult* r = getResultAt(sel);
            if (r && m_onDownload) {
                SearchResult copy = *r;
                FileListService::Entry rf = filesForRow(sel);
                std::vector<TorrentFileEntry> files;
                if (rf.state == FileListState::Loaded) files = rf.files;
                m_onDownload(copy, files);
                return 1;
            }
        }
    }

    return result;
}
