#include "TorrentListWidget.h"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

const TorrentListWidget::ColumnInfo TorrentListWidget::COLUMN_INFO[COL_COUNT] = {
    {"Name",        300, FL_ALIGN_LEFT},
    {"Size",        80,  FL_ALIGN_RIGHT},
    {"Progress",    100, FL_ALIGN_CENTER},
    {"Status",      100, FL_ALIGN_CENTER},
    {"Down Speed",  100, FL_ALIGN_RIGHT},
    {"Up Speed",    100, FL_ALIGN_RIGHT},
    {"ETA",         80,  FL_ALIGN_RIGHT},
    {"Ratio",       60,  FL_ALIGN_RIGHT},
    {"Peers",       60,  FL_ALIGN_RIGHT}
};

// Colors are now dynamic

TorrentListWidget::TorrentListWidget(int x, int y, int w, int h, const char* label)
    : Fl_Table_Row(x, y, w, h, label)
    , m_sortColumn(COL_NAME)
    , m_sortAscending(true)
    , m_dropHighlight(false)
{
    initializeColumns();
    
    // Table settings
    type(SELECT_MULTI);
    when(FL_WHEN_RELEASE);
    end();
}

TorrentListWidget::~TorrentListWidget() {
}

void TorrentListWidget::initializeColumns() {
    // Set up columns
    cols(COL_COUNT);
    col_header(1);
    col_resize(1);
    
    // Set column widths
    for (int i = 0; i < COL_COUNT; i++) {
        col_width(i, COLUMN_INFO[i].width);
    }
    
    // Row settings
    row_height_all(25);
    row_header(0);
}

void TorrentListWidget::setTorrents(const std::vector<TorrentItem*>& torrents) {
    m_torrents = torrents;
    updateSortedIndices();
    rows(m_torrents.size());
    redraw();
}

void TorrentListWidget::addTorrent(TorrentItem* torrent) {
    if (!torrent) return;
    
    m_torrents.push_back(torrent);
    updateSortedIndices();
    rows(m_torrents.size());
    redraw();
}

void TorrentListWidget::removeTorrent(const std::string& hash) {
    auto it = std::remove_if(m_torrents.begin(), m_torrents.end(),
        [&hash](TorrentItem* item) {
            return item && item->getHash() == hash;
        });
    
    if (it != m_torrents.end()) {
        // Clear selection to avoid graphical glitches with indices
        for (int i = 0; i < rows(); i++) {
            select_row(i, 0);
        }
        
        m_torrents.erase(it, m_torrents.end());
        updateSortedIndices();
        rows(m_torrents.size());
        redraw();
    }
}

void TorrentListWidget::updateTorrent(TorrentItem* torrent) {
    // Just redraw, the torrent object is already updated
    redraw();
}

void TorrentListWidget::clear() {
    m_torrents.clear();
    m_sortedIndices.clear();
    rows(0);
    redraw();
}

std::vector<TorrentItem*> TorrentListWidget::getSelectedTorrents() {
    std::vector<TorrentItem*> selected;
    
    for (int i = 0; i < this->rows(); i++) {
        if (this->row_selected(i)) {
            TorrentItem* torrent = getTorrentAt(i);
            if (torrent) {
                selected.push_back(torrent);
            }
        }
    }
    
    return selected;
}

TorrentItem* TorrentListWidget::getSelectedTorrent() {
    for (int i = 0; i < this->rows(); i++) {
        if (this->row_selected(i)) {
            return getTorrentAt(i);
        }
    }
    return nullptr;
}

bool TorrentListWidget::hasSelection() {
    for (int i = 0; i < this->rows(); i++) {
        if (this->row_selected(i)) {
            return true;
        }
    }
    return false;
}

void TorrentListWidget::sortBy(Column column, bool ascending) {
    m_sortColumn = column;
    m_sortAscending = ascending;
    updateSortedIndices();
    redraw();
}

void TorrentListWidget::updateSortedIndices() {
    m_sortedIndices.clear();
    m_sortedIndices.reserve(m_torrents.size());
    
    for (size_t i = 0; i < m_torrents.size(); i++) {
        m_sortedIndices.push_back(i);
    }
    
    // Sort indices based on current sort column
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [this](int a, int b) { return compareTorrents(a, b); });
}

bool TorrentListWidget::compareTorrents(int idx1, int idx2) const {
    TorrentItem* t1 = m_torrents[idx1];
    TorrentItem* t2 = m_torrents[idx2];
    
    if (!t1 || !t2) return false;
    
    bool result = false;
    
    switch (m_sortColumn) {
        case COL_NAME:
            result = t1->getName() < t2->getName();
            break;
        case COL_SIZE:
            result = t1->getTotalSize() < t2->getTotalSize();
            break;
        case COL_PROGRESS:
            result = t1->getProgress() < t2->getProgress();
            break;
        case COL_STATUS:
            result = t1->getState() < t2->getState();
            break;
        case COL_DOWN_SPEED:
            result = t1->getDownloadRate() < t2->getDownloadRate();
            break;
        case COL_UP_SPEED:
            result = t1->getUploadRate() < t2->getUploadRate();
            break;
        case COL_ETA:
            result = t1->getETA() < t2->getETA();
            break;
        case COL_RATIO:
            result = t1->getRatio() < t2->getRatio();
            break;
        case COL_PEERS:
            result = t1->getNumPeers() < t2->getNumPeers();
            break;
        default:
            result = false;
    }
    
    return m_sortAscending ? result : !result;
}

TorrentItem* TorrentListWidget::getTorrentAt(int row) const {
    if (row < 0 || row >= (int)m_sortedIndices.size()) {
        return nullptr;
    }
    
    int idx = m_sortedIndices[row];
    if (idx < 0 || idx >= (int)m_torrents.size()) {
        return nullptr;
    }
    
    return m_torrents[idx];
}

void TorrentListWidget::draw_cell(TableContext context, int row, int col,
                                  int x, int y, int w, int h)
{
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

void TorrentListWidget::drawHeader(int col, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    
    // Draw background
    fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, COLOR_HEADER_BG);
    
    // Draw text
    if (col >= 0 && col < COL_COUNT) {
        fl_color(FL_FOREGROUND_COLOR);
        fl_font(FL_HELVETICA_BOLD, 12);
        
        std::string label = COLUMN_INFO[col].name;
        
        // Add sort indicator
        if (col == m_sortColumn) {
            label += m_sortAscending ? " ▲" : " ▼";
        }
        
        fl_draw(label.c_str(), x + 5, y, w - 10, h, COLUMN_INFO[col].alignment);
    }
    
    fl_pop_clip();
}

void TorrentListWidget::drawCell(int row, int col, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    
    TorrentItem* torrent = getTorrentAt(row);
    if (!torrent) {
        fl_pop_clip();
        return;
    }
    
    // Background
    Fl_Color bg_color = getRowColor(row);
    fl_color(bg_color);
    fl_rectf(x, y, w, h);
    
    // Text color
    fl_color(getTextColor(row));
    fl_font(FL_HELVETICA, 12);
    
    std::string text;
    
    switch (col) {
        case COL_NAME:
            text = torrent->getName();
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
            
        case COL_SIZE:
            text = torrent->formatSize(torrent->getTotalSize());
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
            
        case COL_PROGRESS:
            drawProgressBar(torrent->getProgress(), x + 5, y + 5, w - 10, h - 10);
            break;
            
        case COL_STATUS:
            text = torrent->getStateString();
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_CENTER | FL_ALIGN_CLIP);
            break;
            
        case COL_DOWN_SPEED:
            text = torrent->formatSpeed(torrent->getDownloadRate());
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
            
        case COL_UP_SPEED:
            text = torrent->formatSpeed(torrent->getUploadRate());
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
            
        case COL_ETA:
            text = torrent->getETAString();
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
            
        case COL_RATIO: {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << torrent->getRatio();
            text = oss.str();
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
        }
            
        case COL_PEERS: {
            std::ostringstream oss;
            oss << torrent->getNumPeers() << " (" << torrent->getNumSeeds() << ")";
            text = oss.str();
            fl_draw(text.c_str(), x + 5, y, w - 10, h, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            break;
        }
    }
    
    fl_pop_clip();
}

void TorrentListWidget::drawProgressBar(double progress, int x, int y, int w, int h) {
    // Background
    fl_color(getProgressBgColor());
    fl_rectf(x, y, w, h);
    
    // Progress
    int progress_w = static_cast<int>(w * progress);
    fl_color(getProgressFgColor());
    fl_rectf(x, y, progress_w, h);
    
    // Border
    fl_color(FL_FOREGROUND_COLOR);
    fl_rect(x, y, w, h);
    
    // Text
    fl_color(FL_FOREGROUND_COLOR);
    fl_font(FL_HELVETICA, 10);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << (progress * 100.0) << "%";
    std::string text = oss.str();
    
    fl_draw(text.c_str(), x, y, w, h, FL_ALIGN_CENTER);
}

// Helper: parse a "file://" URI list (newline-separated) into file paths
static std::vector<std::string> parseDroppedPaths(const char* text) {
    std::vector<std::string> paths;
    if (!text) return paths;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip \r if present (Windows line endings in text)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Strip "file://" prefix (Linux DnD)
        if (line.substr(0, 7) == "file://") {
            line = line.substr(7);
        }
        // URL-decode %20 etc (minimal: only %20 for spaces)
        std::string decoded;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '%' && i + 2 < line.size()) {
                int val = 0;
                char h1 = line[i+1], h2 = line[i+2];
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    return -1;
                };
                int v1 = hex(h1), v2 = hex(h2);
                if (v1 >= 0 && v2 >= 0) {
                    decoded += (char)(v1 * 16 + v2);
                    i += 2;
                    continue;
                }
            }
            decoded += line[i];
        }
        if (!decoded.empty()) paths.push_back(decoded);
    }
    return paths;
}

int TorrentListWidget::handle(int event) {
    // ── Drag-and-drop events (Linux/X11 via FLTK) ─────────────────────────
    if (event == FL_DND_ENTER || event == FL_DND_DRAG) {
        if (!m_dropHighlight) {
            m_dropHighlight = true;
            redraw();
        }
        return 1; // Accept the drag
    }
    if (event == FL_DND_LEAVE) {
        if (m_dropHighlight) {
            m_dropHighlight = false;
            redraw();
        }
        return 1;
    }
    if (event == FL_DND_RELEASE) {
        // Accept paste that will follow
        m_dropHighlight = false;
        redraw();
        return 1;
    }
    if (event == FL_PASTE) {
        // Fired after FL_DND_RELEASE; contains the URI list
        const char* text = Fl::event_text();
        if (text && m_onDropCallback) {
            auto paths = parseDroppedPaths(text);
            for (const auto& p : paths) {
                // Only accept .torrent files
                if (p.size() > 8 &&
                    p.substr(p.size() - 8) == ".torrent") {
                    m_onDropCallback(p);
                }
            }
        }
        return 1;
    }
    // ─────────────────────────────────────────────────────────────────────

    int result = Fl_Table_Row::handle(event);
    
    // Handle column header clicks for sorting
    if (event == FL_RELEASE) {
        if (callback_context() == CONTEXT_COL_HEADER) {
            // Only sort if it wasn't a resize operation (drag)
            // Fl::event_is_click() returns true if the mouse hasn't moved "much"
            if (Fl::event_is_click()) {
                int col = callback_col();
                if (col >= 0 && col < COL_COUNT) {
                    // Check if we are clicking the same column
                    if ((Column)col == m_sortColumn) {
                        // Toggle order
                        sortBy((Column)col, !m_sortAscending);
                    } else {
                        bool defaultAscending = true;
                        if (col == COL_SIZE || col == COL_PROGRESS || 
                            col == COL_DOWN_SPEED || col == COL_UP_SPEED || 
                            col == COL_RATIO || col == COL_PEERS) {
                            defaultAscending = false;
                        }
                        sortBy((Column)col, defaultAscending);
                    }
                    return 1; // Event handled
                }
            }
        }
    }
    
    // Handle double-click on rows
    if (event == FL_PUSH && Fl::event_clicks() > 0) {
        if (callback_context() == CONTEXT_CELL) {
            TorrentItem* torrent = getSelectedTorrent();
            if (torrent) {
                showDetailsDialog(torrent);
            }
        }
    }
    
    return result;
}

#include "TorrentDetailsDialog.h"

void TorrentListWidget::showDetailsDialog(TorrentItem* torrent) {
    TorrentDetailsDialog* dlg = new TorrentDetailsDialog(torrent);
    dlg->show_modal();
    delete dlg;
}

void TorrentListWidget::drawDropOverlay() {
    // Get the inner table area (excludes headers/scrollbars)
    int tx = x(), ty = y(), tw = w(), th = h();

    // Semi-transparent green overlay (simulate with a translucent rect)
    // FLTK doesn't support true alpha, so we use a solid tinted color
    fl_color(fl_rgb_color(20, 100, 50));
    fl_rectf(tx, ty, tw, th);

    // Dashed border box (inner margin)
    int margin = 30;
    int bx = tx + margin, by = ty + margin;
    int bw = tw - margin * 2, bh = th - margin * 2;

    fl_color(fl_rgb_color(80, 255, 100));
    fl_line_style(FL_DASH, 3);
    fl_rect(bx, by, bw, bh);
    fl_line_style(0); // Reset line style

    // Centered text
    fl_color(FL_WHITE);
    fl_font(FL_HELVETICA_BOLD, 22);
    fl_draw("Drop .torrent files here",
            bx, by, bw, bh,
            FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    fl_color(fl_rgb_color(180, 200, 230));
    fl_font(FL_HELVETICA, 13);
    // Draw hint below the main text
    fl_draw("or click \"Add Torrent\" to browse",
            bx, by + 38, bw, bh,
            FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
}

void TorrentListWidget::draw() {
    // Draw normal table content first
    Fl_Table_Row::draw();
    // If a file is being dragged over us, paint the overlay on top
    if (m_dropHighlight) {
        fl_push_clip(x(), y(), w(), h());
        drawDropOverlay();
        fl_pop_clip();
    }
}

Fl_Color TorrentListWidget::getRowColor(int row) const {
    if (const_cast<TorrentListWidget*>(this)->row_selected(row)) return FL_SELECTION_COLOR;
    
    bool darkMode = fl_gray_ramp(0) == FL_BLACK; // Simple check if dark mode is active
    
    if (row % 2 == 0) return FL_BACKGROUND2_COLOR;
    
    // For odd rows, we want a slight variation
    uchar r, g, b;
    Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
    if (r > 128) {
        return fl_rgb_color(r - 10, g - 10, b - 10);
    } else {
        return fl_rgb_color(r + 10, g + 10, b + 10);
    }
}

Fl_Color TorrentListWidget::getTextColor(int row) const {
    if (const_cast<TorrentListWidget*>(this)->row_selected(row)) return FL_WHITE;
    return FL_FOREGROUND_COLOR;
}

Fl_Color TorrentListWidget::getProgressBgColor() const {
    uchar r, g, b;
    Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
    if (r > 128) {
        return fl_rgb_color(200, 200, 200);
    } else {
        return fl_rgb_color(60, 60, 60);
    }
}

Fl_Color TorrentListWidget::getProgressFgColor() const {
    return fl_rgb_color(50, 150, 50); // Keep it green
}
