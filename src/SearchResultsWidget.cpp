#include "SearchResultsWidget.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include "ArchiveCheckTask.h"
#include "Format.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"

#include <algorithm>
#include <string>
#include <vector>

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

SearchResultsWidget::~SearchResultsWidget() {
    // A lookup started from this table may still be running; its completion
    // must not touch rows that no longer exist.
    if (m_alive) *m_alive = false;
    // The archive checks stop too, instead of running out their deadlines in
    // swarms nobody is waiting for.
    for (const auto& task : m_archiveTasks) {
        if (task.second) task.second->cancel();
    }
    clearBadgeIcons();
}

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
    m_reports.clear();
    m_reputation.clear();
    m_reputationPending.clear();
    m_archive.clear();
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
    if (m_darkMode != darkMode) {
        // Ink art has to be flipped for a light row, so the cached copies no
        // longer suit the new background. A coloured icon comes out the same
        // either way, but rebuilding it costs nothing.
        m_darkMode = darkMode;
        clearBadgeIcons();
    }
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
        case FileListState::Loaded: {
            // One summary line, then the files, then an "and N more" line
            // when the list is longer than the row shows.
            lines = 1 + static_cast<int>(rf.files.size());
            const int maxFiles = MAX_SHOWN_FILES + 1;
            if (lines > 1 + maxFiles) lines = 1 + maxFiles;
            // A reputation answer the user asked for gets its own line.
            if (m_reputation.find(cacheKey(*r)) != m_reputation.end()) ++lines;
            // The small print always shows the hash, or says there is none.
            ++lines;
            // An archive line is always offered, and grows when it answers:
            // the entries it listed are drawn under it.
            if (archiveIndexFor(rf.files) >= 0) {
                ++lines;
                auto action = m_archive.find(cacheKey(*r));
                if (action != m_archive.end()) {
                    lines += archiveEntryRows(action->second);
                }
            }
            break;
        }
        case FileListState::Queued:
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

// The triangle is drawn inside the top slice of the row, so the height of the
// cell is not needed here.
void SearchResultsWidget::drawExpandCell(int row, int x, int y, int w, int) {
    const SearchResult* r = getResultAt(row);
    if (!r) return;

    bool expanded = isExpanded(cacheKey(*r));
    FileListService::Entry rf = filesForRow(row);

    Fl_Color fg = FL_FOREGROUND_COLOR;
    if (rf.state == FileListState::Loading || rf.state == FileListState::Queued) {
        fg = fl_rgb_color(140, 140, 140);
    }

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
        case COL_SIZE:     text = r->size > 0 ? Format::bytes(r->size) : "?"; break;
        case COL_SEEDERS:  text = r->seeders >= 0 ? std::to_string(r->seeders) : "?"; break;
        case COL_LEECHERS: text = r->leechers >= 0 ? std::to_string(r->leechers) : "?"; break;
        case COL_SOURCE:   text = r->source; break;
        default: break;
    }

    // Pixel-measured ellipsis: full absolute name when the maximised Name
    // column has room, "..." only when it really overflows (square window).
    // FL_ALIGN_CLIP is the second net, same as TorrentListWidget uses.
    // The main value always lives in the top BASE_ROW_HEIGHT slice so an
    // expanded (taller) row keeps name/size/seeders aligned with the toggle
    // triangle instead of sinking to the vertical centre. The file list owns
    // everything below that slice.
    const int topH = h < BASE_ROW_HEIGHT ? h : BASE_ROW_HEIGHT;

    int textX = x + 5;
    int textW = w - 10;
    if (col == COL_NAME) {
        // The shield sits immediately before the torrent name on every
        // surface, so the risk is visible before the row is expanded.
        const int shieldY = y + (topH - SHIELD_BOX) / 2;
        const SecurityReport& report = reportForRow(row);
        SecurityBadge::drawShield(x + 5, shieldY, SHIELD_BOX, SHIELD_BOX,
                                  report.level);
        fl_color(FL_FOREGROUND_COLOR);
        textX = x + 5 + SHIELD_BOX + 4;
        int reserved = SHIELD_BOX + 14;

        // Content badges, immediately after the shield: the indicators read as
        // one tag group, so risk colour and content type are seen together. A
        // known file list draws them solid; until it is known, a title that
        // ends in the matching extension draws the dimmed hint. Once the list
        // is loaded the title can no longer add anything, so a hint can never
        // contradict the files the row actually downloads.
        const bool fileListKnown =
            m_files.state(cacheKey(*r)) == FileListState::Loaded;
        struct BadgePlan {
            ContentBadge badge;
            bool confirmed;
            bool hinted;
        };
        const BadgePlan plan[] = {
            {ContentBadge::Archive, report.archives > 0,
             !fileListKnown &&
                 SecurityAnalyzer::titleSuggestsArchive(r->name)},
            // The lock never comes from the title or from the torrent's file
            // list: only a completed archive check that found entries asking
            // for a password can raise it.
            {ContentBadge::Encrypted, encryptedForRow(row) > 0, false},
            {ContentBadge::DiscImage, report.discImages > 0,
             !fileListKnown &&
                 SecurityAnalyzer::titleSuggestsDiscImage(r->name)},
            // Video is never inferred from a title: a real video file proves
            // it, "1080p" in a name does not.
            {ContentBadge::Video, report.videos > 0, false},
            {ContentBadge::NativeExecutable, report.nativeExecutables > 0,
             !fileListKnown &&
                 SecurityAnalyzer::titleSuggestsNativeExecutable(r->name)}
        };
        for (const BadgePlan& item : plan) {
            if (!item.confirmed && !item.hinted) continue;
            Fl_Image* badge = badgeIcon(item.badge, item.hinted);
            if (!badge) continue;
            const int needed = BADGE_BOX + BADGE_GAP;
            if (w - (reserved + needed) < MIN_BADGED_NAME_WIDTH) break;
            // The picture is already squared to the slot; centring only
            // matters if a source asset is smaller than BADGE_BOX.
            const int badgeX = textX + (BADGE_BOX - badge->w()) / 2;
            badge->draw(badgeX, y + (topH - badge->h()) / 2);
            textX += needed;
            reserved += needed;
        }
        fl_color(FL_FOREGROUND_COLOR);
        textW = w - reserved;
    }
    if (textW < 0) textW = 0;
    if (textW > 0 && !text.empty()) text = elideToWidth(text, textW);

    const Fl_Align align =
        static_cast<Fl_Align>(COLUMN_INFO[col].alignment | FL_ALIGN_CLIP);
    fl_draw(text.c_str(), textX, y, textW, topH, align);

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
    // The archive line under an expanded row is the only control that starts
    // the two-piece read.
    if (event == FL_RELEASE) {
        int R = 0, C = 0;
        ResizeFlag resizeFlag = RESIZE_NONE;
        if (cursor2rowcol(R, C, resizeFlag) == CONTEXT_CELL &&
            archiveLineHit(R, Fl::event_x(), Fl::event_y())) {
            const SearchResult* result = getResultAt(R);
            if (result) {
                startArchiveCheck(cacheKey(*result));
                return 1;
            }
        }
    }

    // A click on the shield asks about the torrent's hash. Handled here, before
    // the table sees the event, so the click neither selects nor expands the
    // row: it only asks the question the shield stands for.
    if (event == FL_PUSH) {
        int R = 0, C = 0;
        ResizeFlag resizeFlag = RESIZE_NONE;
        if (cursor2rowcol(R, C, resizeFlag) == CONTEXT_CELL && C == COL_NAME &&
            shieldHit(R, Fl::event_x(), Fl::event_y())) {
            m_shieldPressedRow = R;
            return 1;
        }
    } else if (event == FL_RELEASE && m_shieldPressedRow >= 0) {
        const int row = m_shieldPressedRow;
        m_shieldPressedRow = -1;
        if (shieldHit(row, Fl::event_x(), Fl::event_y())) {
            onShieldClicked(row);
            return 1;
        }
    }

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
                if (rf.state == FileListState::Loaded) {
                    files = rf.files;
                    m_onDownload(copy, files, rf.torrentBody);
                } else {
                    m_onDownload(copy, files, std::string());
                }
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
                m_onDownload(copy, files, rf.torrentBody);
                return 1;
            }
        }
    }

    return result;
}
