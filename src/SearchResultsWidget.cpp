#include "SearchResultsWidget.h"
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include "ArchiveInspector.h"
#include "ArchivePreflight.h"
#include "AssetLoader.h"
#include "ReputationService.h"
#include "SecurityAnalyzer.h"
#include "SecurityBadge.h"
#include "TorrentItem.h"   // formatSize, used for the size column

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
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

// Box the content badges are fitted into inside the Name cell. Tall enough to
// read the artwork: at 15 px the assets turn into a smudge.
const int BADGE_WIDTH = 21;
const int BADGE_HEIGHT = 18;
const int BADGE_GAP = 4;

// The badges may not eat the name: below this many pixels the row stops adding
// them, so a narrow column still shows the torrent it is describing. Distinct
// from MIN_NAME_WIDTH, which is the column's own floor.
const int MIN_BADGED_NAME_WIDTH = 40;

// How much of the badge shows when it comes from the title alone. Dimmed on
// purpose: the title is written by the publisher, so this is a hint, and it
// must not look like the badge the real file list produces.
const double HINT_ALPHA = 0.55;

// A picture made of white or grey strokes has no colour of its own: max(r,g,b)
// and min(r,g,b) stay close on every visible pixel. A coloured icon such as a
// green archive fill does not. The 5% allowance keeps antialiased edges from
// classifying a coloured picture as ink.
bool isInkArt(const Fl_RGB_Image* rgb) {
    if (!rgb || !rgb->array || rgb->w() <= 0 || rgb->h() <= 0) return false;
    const int d = rgb->d();
    if (d != 3 && d != 4) return false;

    long visible = 0;
    long coloured = 0;
    for (int y = 0; y < rgb->h(); ++y) {
        for (int x = 0; x < rgb->w(); ++x) {
            const size_t i = (static_cast<size_t>(y) * rgb->w() + x) * d;
            if (d == 4 && rgb->array[i + 3] < 16) continue;
            const int r = rgb->array[i];
            const int g = rgb->array[i + 1];
            const int b = rgb->array[i + 2];
            const int high = std::max({r, g, b});
            const int low = std::min({r, g, b});
            if (high - low > 32) ++coloured;
            ++visible;
        }
    }
    return visible > 0 && coloured * 20 < visible;
}

// The badge asset may be neutral ink (white-on-black line art) or a coloured
// icon that carries its own fills. Ink art vanishes on a light row and has to
// be flipped; a coloured icon must be left exactly as designed, because
// inverting a green fill would turn it magenta. isInkArt() tells the two apart
// and variantCopy() applies the flip and/or the fade.
//
// 'alphaScale' fades the whole picture, which is how a badge taken from the
// title alone is drawn: present, but visibly not the confirmed one. Returns
// nullptr when the source cannot be transformed, and the caller falls back to
// the picture as loaded.
Fl_Image* variantCopy(Fl_Image* source, bool invert, double alphaScale) {
    const Fl_RGB_Image* rgb = dynamic_cast<const Fl_RGB_Image*>(source);
    if (!rgb || !rgb->array || rgb->w() <= 0 || rgb->h() <= 0) return nullptr;
    const int d = rgb->d();
    if (d != 3 && d != 4) return nullptr;
    if (alphaScale < 1.0 && d != 4) return nullptr;   // no alpha to fade

    const int w = rgb->w();
    const int h = rgb->h();
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) *
                         static_cast<size_t>(d);
    unsigned char* pixels = new unsigned char[bytes];
    for (size_t i = 0; i < bytes; i += static_cast<size_t>(d)) {
        for (int channel = 0; channel < 3; ++channel) {
            const unsigned char value =
                static_cast<unsigned char>(rgb->array[i + channel]);
            pixels[i + channel] = invert
                ? static_cast<unsigned char>(255 - value)
                : value;
        }
        if (d == 4) {
            const double alpha = static_cast<unsigned char>(rgb->array[i + 3]);
            pixels[i + 3] = static_cast<unsigned char>(alpha * alphaScale + 0.5);
        }
    }

    Fl_RGB_Image* image = new Fl_RGB_Image(pixels, w, h, d);
    image->alloc_array = 1;   // the image frees the buffer it was given
    return image;
}

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

SearchResultsWidget::~SearchResultsWidget() {
    // A lookup started from this table may still be running; its completion
    // must not touch rows that no longer exist.
    if (m_alive) *m_alive = false;
    clearBadgeIcons();
}

void SearchResultsWidget::clearBadgeIcons() {
    for (BadgeArt& art : m_badgeArt) {
        delete art.confirmed;
        art.confirmed = nullptr;
        delete art.hint;
        art.hint = nullptr;
    }
}

Fl_Image* SearchResultsWidget::badgeIcon(ContentBadge badge, bool hinted) {
    const int index = static_cast<int>(badge);
    if (index < 0 || index >= static_cast<int>(ContentBadge::Count)) {
        return nullptr;
    }
    BadgeArt& art = m_badgeArt[index];
    Fl_Image*& cached = hinted ? art.hint : art.confirmed;
    if (cached) return cached;

    const char* asset = nullptr;
    switch (badge) {
        case ContentBadge::Archive:          asset = "compressed.png"; break;
        case ContentBadge::DiscImage:        asset = "iso.png"; break;
        case ContentBadge::Video:            asset = "video.png"; break;
        case ContentBadge::NativeExecutable: asset = "executable.png"; break;
        case ContentBadge::Count:            break;
    }
    if (!asset) return nullptr;

    // Fitted rather than squared: the assets have their own aspect ratio, and
    // squeezing one into a square would distort it.
    std::unique_ptr<Fl_Image> loaded(
        AssetLoader::loadFit(asset, BADGE_WIDTH, BADGE_HEIGHT));
    if (!loaded) return nullptr;   // no asset: no badge, never a blank square

    // Ink art needs its strokes flipped on a light row; a coloured icon is
    // drawn as designed on both. See isInkArt() and variantCopy().
    const Fl_RGB_Image* rgb = dynamic_cast<const Fl_RGB_Image*>(loaded.get());
    const bool invert = !m_darkMode && isInkArt(rgb);
    cached = variantCopy(loaded.get(), invert, hinted ? HINT_ALPHA : 1.0);
    if (!cached) cached = loaded.release();
    return cached;
}

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

const SecurityReport& SearchResultsWidget::reportForRow(int row) {
    static const SecurityReport unknown = [] {
        SecurityReport report;
        report.summary = "No file list available. Metadata risk is unknown.";
        return report;
    }();

    const SearchResult* r = getResultAt(row);
    if (!r) return unknown;

    const std::string key = cacheKey(*r);
    auto cached = m_reports.find(key);
    if (cached != m_reports.end()) return cached->second;

    const FileListService::Entry rf = filesForRow(row);
    if (rf.state != FileListState::Loaded) return unknown;

    auto inserted = m_reports.emplace(key, SecurityAnalyzer::analyze(rf.files));
    return inserted.first->second;
}

bool SearchResultsWidget::shieldHit(int row, int x, int y) {
    if (row < 0 || row >= rows()) return false;
    int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
    if (!find_cell(CONTEXT_CELL, row, COL_NAME, cellX, cellY, cellW, cellH)) {
        return false;
    }
    (void)cellW;
    (void)cellH;
    // Same 15 px box the shield is drawn in, in the top slice of the row.
    constexpr int SHIELD = 15;
    return x >= cellX + 5 && x <= cellX + 5 + SHIELD && y >= cellY &&
           y <= cellY + SHIELD + 4;
}

int SearchResultsWidget::archiveIndexFor(
    const std::vector<TorrentFileEntry>& files) const {
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (ArchiveInspector::isArchivePath(files[i].path)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SearchResultsWidget::archiveLineHit(int row, int x, int y) {
    const SearchResult* result = getResultAt(row);
    if (!result) return false;
    auto action = m_archive.find(cacheKey(*result));
    if (action == m_archive.end() || !action->second.visible) return false;

    int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
    if (!find_cell(CONTEXT_CELL, row, COL_NAME, cellX, cellY, cellW, cellH)) {
        return false;
    }
    (void)cellY;
    (void)cellH;
    if (x < cellX || x > cellX + cellW) return false;
    return y >= action->second.top && y <= action->second.bottom;
}

void SearchResultsWidget::startArchiveCheck(const std::string& key) {
    auto action = m_archive.find(key);
    if (action == m_archive.end() || action->second.running) return;

    ArchiveAction& entry = action->second;
    if (entry.magnet.empty() && entry.torrentUrl.empty()) {
        entry.line =
            "Archive: this result carries neither a .torrent nor a magnet.";
        redraw();
        return;
    }

    entry.running = true;
    entry.line.clear();

    struct Delivery {
        SearchResultsWidget* widget = nullptr;
        std::shared_ptr<bool> alive;
        std::string key;
        std::string line;
    };

    const int index = entry.fileIndex;
    const std::string path = entry.path;
    const std::string magnet = entry.magnet;
    const std::string torrentUrl = entry.torrentUrl;
    std::shared_ptr<bool> alive = m_alive;
    SearchResultsWidget* self = this;

    std::thread([self, alive, key, index, path, magnet, torrentUrl]() {
        std::vector<SecurityFile> entries;
        std::string error;
        std::atomic<bool> cancelled{false};
        const bool ok = ArchivePreflight::inspect(magnet, torrentUrl, index,
                                                  entries, error, &cancelled);

        Delivery* delivery = new Delivery;
        delivery->widget = self;
        delivery->alive = alive;
        delivery->key = key;
        delivery->line =
            ok ? archiveSummary(path, entries)
               : "Archive: the index could not be read (" + error + ").";
        Fl::awake([](void* data) {
            std::unique_ptr<Delivery> delivery(static_cast<Delivery*>(data));
            if (!delivery->alive || !*delivery->alive) return;
            SearchResultsWidget* widget = delivery->widget;
            auto found = widget->m_archive.find(delivery->key);
            if (found != widget->m_archive.end()) {
                found->second.running = false;
                found->second.line = delivery->line;
            }
            widget->forEachRow([&](int row, const SearchResult& r) {
                if (SearchResultsWidget::cacheKey(r) == delivery->key) {
                    widget->updateRowHeight(row);
                }
            });
            widget->redraw();
        }, delivery);
    }).detach();

    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
}

void SearchResultsWidget::onShieldClicked(int row) {
    const SearchResult* result = getResultAt(row);
    if (!result) return;

    const std::string key = cacheKey(*result);
    const FileListService::Entry entry = filesForRow(row);
    if (entry.state != FileListState::Loaded || entry.files.empty()) {
        // No hashes yet. The click is the request for them: fetch the list and
        // run the lookup the moment it lands. This is still one user action,
        // not a background query.
        m_reputationPending[key] = true;
        requestFiles(row);
        redraw();
        return;
    }
    startReputationLookup(key, entry.files);
}

void SearchResultsWidget::startReputationLookup(
    const std::string& key, const std::vector<TorrentFileEntry>& files) {
    const ReputationTarget target = findReputationTarget(files);

    if (!target.found()) {
        m_reputation[key] = ReputationService::noHashResult();
        forEachRow([&](int row, const SearchResult& r) {
            if (cacheKey(r) == key) updateRowHeight(row);
        });
        redraw();
        return;
    }

    ReputationService& service = sharedReputationService();
    std::shared_ptr<bool> alive = m_alive;
    SearchResultsWidget* self = this;
    const ReputationService::Start start = service.request(
        target.sha256, [self, alive, key](const ReputationResult& answer) {
            if (!alive || !*alive) return;
            self->m_reputation[key] = answer;
            self->forEachRow([&](int row, const SearchResult& r) {
                if (SearchResultsWidget::cacheKey(r) == key) {
                    self->updateRowHeight(row);
                }
            });
            self->redraw();
        });

    switch (start) {
        case ReputationService::Start::Started:
        case ReputationService::Start::AlreadyRunning: {
            ReputationResult querying;
            querying.state = ReputationState::Querying;
            querying.sha256 = target.sha256;
            m_reputation[key] = querying;
            break;
        }
        case ReputationService::Start::Cached:
            m_reputation[key] = service.find(target.sha256);
            break;
        case ReputationService::Start::Rejected: {
            ReputationResult rejected;
            rejected.state = ReputationState::Error;
            rejected.sha256 = target.sha256;
            rejected.message =
                "Too many reputation checks are running. Try again in a moment.";
            m_reputation[key] = rejected;
            break;
        }
    }

    forEachRow([&](int row, const SearchResult& r) {
        if (cacheKey(r) == key) updateRowHeight(row);
    });
    redraw();
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
    // The list changed, so the cached report for it is stale.
    m_reports.erase(key);

    // A shield clicked before the list was known asked for this: now that the
    // hashes are here, run it.
    auto pending = m_reputationPending.find(key);
    if (pending != m_reputationPending.end()) {
        m_reputationPending.erase(pending);
        const FileListService::Entry entry = m_files.find(key);
        if (entry.state == FileListState::Loaded) {
            startReputationLookup(key, entry.files);
        }
    }

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
            // An archive line is always offered, and grows when it answers.
            if (archiveIndexFor(rf.files) >= 0) ++lines;
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

// The little triangle that opens the file list, plus a hint of the state.
void SearchResultsWidget::drawExpandCell(int row, int x, int y, int w, int h) {
    (void)h;   // The triangle is drawn inside the top slice of the row.
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

    if (rf.state == FileListState::Loading || rf.state == FileListState::Queued) {
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

    // Reputation, when the user asked for it, sits above the metadata summary:
    // it is the answer to the question the shield click asked.
    {
        auto asked = m_reputation.find(cacheKey(*r));
        if (asked != m_reputation.end()) {
            const std::string line = elideToWidth(
                reputationLine(asked->second), avail);
            fl_color(reputationHasDetections(asked->second)
                         ? SecurityBadge::riskColor(RiskLevel::Critical)
                         : FL_FOREGROUND_COLOR);
            fl_font(FL_HELVETICA, 11);
            fl_draw(line.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                    FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            int x2 = x + w - 6;
            if (x2 < x + 24) x2 = x + 24;
            fl_color(sepColor);
            fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2,
                    ly + FILE_LINE_HEIGHT - 1);
            ly += FILE_LINE_HEIGHT;
        }
    }

    // Summary line first: the user expanded the row to see what is inside,
    // and the answer to "is any of this runnable?" belongs at the top.
    const SecurityReport& report = reportForRow(row);
    {
        const std::string summary = elideToWidth(
            SecurityBadge::riskLabel(report.level) + "  -  " + report.summary,
            avail);
        fl_color(SecurityBadge::riskColor(report.level));
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_draw(summary.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_color(sepColor);
        int x2 = x + w - 6;
        if (x2 < x + 24) x2 = x + 24;
        fl_line(x + 24, ly + FILE_LINE_HEIGHT - 1, x2, ly + FILE_LINE_HEIGHT - 1);
        ly += FILE_LINE_HEIGHT;
    }

    int shown = static_cast<int>(rf.files.size());
    if (shown > MAX_SHOWN_FILES) shown = MAX_SHOWN_FILES;
    for (int i = 0; i < shown && ly + FILE_LINE_HEIGHT <= limit + 1; ++i) {
        const TorrentFileEntry& f = rf.files[static_cast<size_t>(i)];
        // The size suffix is never cut: only the path is elided, measured
        // against what is left after the "- " bullet, the suffix and the
        // per-file tags. Full path shows when the window is wide; "..." only
        // when narrow.
        fl_font(FL_HELVETICA, 11);
        const std::string suffix = "  (" + TorrentItem::formatSize(f.size) + ")";
        std::string tagsText;
        const std::vector<std::string> tags = SecurityAnalyzer::tagsForFile(f);
        if (!tags.empty()) {
            tagsText = "  [";
            for (std::size_t t = 0; t < tags.size() && t < 2; ++t) {
                if (t) tagsText += ", ";
                tagsText += tags[t];
            }
            tagsText += "]";
        }
        const int bulletW = static_cast<int>(fl_width("- "));
        const int suffixW = static_cast<int>(fl_width(suffix.c_str()));
        const int tagsW = static_cast<int>(fl_width(tagsText.c_str()));
        std::string path = SecurityBadge::displayText(f.path);
        path = elideToWidth(path, avail - bulletW - suffixW - tagsW);
        const std::string label = "- " + path + suffix + tagsText;
        const FileAssessment assessment = SecurityAnalyzer::assessFile(f);
        fl_color(SecurityBadge::hasWarning(assessment.level)
                     ? SecurityBadge::riskColor(assessment.level)
                     : FL_FOREGROUND_COLOR);
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
        ly += FILE_LINE_HEIGHT;
    }

    // Level 2: one clickable line for the archive in this list. It asks before
    // anything is fetched; the words say what will happen.
    const int archiveIndex = archiveIndexFor(rf.files);
    ArchiveAction& action = m_archive[cacheKey(*r)];
    if (archiveIndex < 0) {
        action.visible = false;
        return;
    }

    const TorrentFileEntry& archive =
        rf.files[static_cast<std::size_t>(archiveIndex)];
    action.fileIndex = archive.fileIndex;
    action.path = archive.path;
    if (action.magnet.empty()) action.magnet = r->magnet;
    if (action.torrentUrl.empty()) action.torrentUrl = r->torrentUrl;

    std::string text;
    Fl_Color color = fl_rgb_color(30, 90, 160);
    if (action.running) {
        text = "Archive: reading the first and last piece...";
        color = SecurityBadge::riskColor(RiskLevel::Medium);
    } else if (!action.line.empty()) {
        text = action.line;
        color = FL_FOREGROUND_COLOR;
    } else {
        text = "+ Check archive " + archive.path + " (first + last piece)";
    }

    if (ly + FILE_LINE_HEIGHT <= limit + 1) {
        fl_font(FL_HELVETICA, 11);
        fl_color(color);
        const std::string shown = elideToWidth(text, avail);
        fl_draw(shown.c_str(), x + 24, ly, avail, FILE_LINE_HEIGHT,
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        action.visible = true;
        action.top = ly;
        action.bottom = ly + FILE_LINE_HEIGHT;
        ly += FILE_LINE_HEIGHT;
    } else {
        action.visible = false;
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
        constexpr int SHIELD = 15;
        const int shieldY = y + (topH - SHIELD) / 2;
        const SecurityReport& report = reportForRow(row);
        SecurityBadge::drawShield(x + 5, shieldY, SHIELD, SHIELD, report.level);
        fl_color(FL_FOREGROUND_COLOR);
        textX = x + 5 + SHIELD + 4;
        int reserved = SHIELD + 14;

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
            const int needed = badge->w() + BADGE_GAP;
            if (w - (reserved + needed) < MIN_BADGED_NAME_WIDTH) break;
            badge->draw(textX, y + (topH - badge->h()) / 2);
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
