#ifndef SEARCHRESULTSWIDGET_H
#define SEARCHRESULTSWIDGET_H

#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Image.H>
#include "SearchEngine.h"
#include "FileListService.h"
#include "SecurityReputation.h"
#include "SecurityTypes.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ArchiveCheckTask;

/**
 * @brief Table showing the results of a torrent search.
 *
 * Modelled on TorrentListWidget so both tables behave the same way (column
 * headers, sorting by clicking them, mouse wheel scrolling, themed
 * scrollbars) while showing a different set of columns.
 */
class SearchResultsWidget : public Fl_Table_Row {
public:
    enum Column {
        COL_EXPAND = 0, // narrow, no header text: the file-list toggle
        COL_NAME,
        COL_SIZE,
        COL_SEEDERS,
        COL_LEECHERS,
        COL_SOURCE,
        COL_COUNT
    };

    SearchResultsWidget(int x, int y, int w, int h,
                        const char* label = nullptr);
    ~SearchResultsWidget() override;

    void setResults(const std::vector<SearchResult>& results);

    // Adds rows without re-sorting the whole table: used while results stream
    // in
    void appendResults(const std::vector<SearchResult>& results);
    void clear();

    // Result under the given row, or nullptr
    const SearchResult* getResultAt(int row) const;
    int selectedRow();
    // Number of results currently shown.
    int rowCount() const { return static_cast<int>(m_results.size()); }

    // File list of the row as currently known. Safe to call from any thread.
    FileListService::Entry filesForRow(int row) const;

    // Opens or closes the file list of a row, exactly as a click on the toggle
    // column does. Opens it with an empty listing and starts the fetch.
    void toggleExpand(int row);
    bool isRowExpanded(int row) const;

    // Starts loading the file list of a row outside the UI thread and returns
    // at once. Does nothing when the list is already known or being loaded.
    // Public for the tests; the table calls it itself on click.
    void requestFiles(int row);
    // Number of file-list fetches this table started that have not come back.
    int pendingLoads() const { return m_files.pending(); }

    // Callback invoked when the user double-clicks a row or presses Enter.
    // The file list is whatever the row has loaded (empty when the row was
    // never expanded or the fetch failed), in torrent order, so the Add
    // dialog can offer one checkbox per file.
    using DownloadCallback = std::function<void(
        const SearchResult&, const std::vector<TorrentFileEntry>&,
        const std::string& torrentBody)>;
    void setOnDownloadCallback(DownloadCallback cb) { m_onDownload = cb; }

protected:
    void draw_cell(TableContext context, int row, int col, int x, int y, int w,
                   int h) override;
    int handle(int event) override;
    void draw() override;

public:
    // Public because MainWindow drives the layout from outside
    void resize(int X, int Y, int W, int H) override;

    // Colors the rows, the header and the scrollbars for the active theme
    void applyTheme(bool darkMode);

private:
    struct ColumnInfo {
        const char* name;
        int width;
        Fl_Align alignment;
    };

    static const ColumnInfo COLUMN_INFO[COL_COUNT];

    // The numbers that decide how a row is drawn. They live in the class so
    // that the drawing code can live in more than one file without repeating
    // them: one place owns the answer.
    static constexpr int BASE_ROW_HEIGHT = 25; // matches initializeColumns()
    static constexpr int FILE_LINE_HEIGHT = 16;
    // Enough to see what is inside without turning a row into a wall of text.
    static constexpr int MAX_SHOWN_FILES = 12;
    // Same idea for the entries an archive check lists: the row shows this
    // many and summarises the rest, which stay visible in Details.
    static constexpr int MAX_SHOWN_ARCHIVE_ENTRIES = 8;
    // Minimum pixel width the Name column is allowed to shrink to. Keeps the
    // table usable at the 720px minimum window width and avoids a zero/negative
    // col_width() when the window is at its smallest.
    static constexpr int MIN_NAME_WIDTH = 120;

    // The shield and every content badge share one box, so the tag group reads
    // as a single size. The shield's polygon is inset one pixel; the badge art
    // keeps its own transparent padding, so both end up visually the same
    // height.
    static constexpr int SHIELD_BOX = 15;
    static constexpr int BADGE_BOX = SHIELD_BOX;
    static constexpr int BADGE_GAP = 4;

    // The badges may not eat the name: below this many pixels the row stops
    // adding them, so a narrow column still shows the torrent it is
    // describing. Distinct from MIN_NAME_WIDTH, which is the column's own
    // floor.
    static constexpr int MIN_BADGED_NAME_WIDTH = 40;

    // How much of the badge shows when it comes from the title alone. Dimmed
    // on purpose: the title is written by the publisher, so this is a hint, and
    // it must not look like the badge the real file list produces.
    static constexpr double HINT_ALPHA = 0.55;

    // Ellipsis by pixels, not by characters: with a proportional font 60 chars
    // can be far narrower (or wider) than the cell. The caller must have set
    // the same fl_font() it will draw with, since fl_width() measures that
    // font. Returns the full text when it fits, so a maximised window shows
    // the absolute name, and only adds "..." when the cell is really too
    // narrow (square window). Uses only fl_width(), available in FLTK 1.3 and
    // 1.4 on every backend we build (Windows x86/ARM64, Linux 32/64, Arch,
    // Flatpak).
    static std::string elideToWidth(const std::string& text, int maxPixels);

    // What is known about the file list of one torrent, and how it was keyed.
    std::vector<SearchResult> m_results;
    std::vector<int> m_sortedIndices;
    Column m_sortColumn;
    bool m_sortAscending;

    Fl_Color m_headerBg;
    Fl_Color m_oddRowColor;

    DownloadCallback m_onDownload;

    // File-list fetches: the cache, the concurrency limit and the threads all
    // belong to the service, not to a table that only has to draw them.
    // Expansion is keyed the same way, so an open row stays open when the user
    // sorts the table. It is view state, touched only on the UI thread.
    FileListService m_files;
    std::unordered_map<std::string, bool> m_expanded;
    // Metadata risk per cache key, computed once when a file list lands. The
    // analyzer is pure and cheap, but draw() runs every frame, so the answer
    // is kept until the list changes.
    std::unordered_map<std::string, SecurityReport> m_reports;
    const SecurityReport& reportForRow(int row);

    // Reputation the user asked for by clicking a shield. Never filled in on
    // its own: a lookup exists only after that click. Keyed like m_reports.
    std::unordered_map<std::string, ReputationResult> m_reputation;
    // Rows whose shield was clicked before their file list was known. The
    // lookup runs as soon as Level 1 lands, because the click was the request.
    std::unordered_map<std::string, bool> m_reputationPending;
    // Cleared in the destructor: a lookup can outlive this table.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    int m_shieldPressedRow = -1;
    bool shieldHit(int row, int x, int y);
    void onShieldClicked(int row);
    void startReputationLookup(const std::string& key,
                               const std::vector<TorrentFileEntry>& files);

    // Content badge drawn in the Name cell right after the shield. The solid
    // version is used only once the file list is known; a title that merely
    // ends in the matching extension gets the dimmed one, so a hint never
    // passes for a fact. Loaded once per type and variant, and rebuilt when the
    // theme changes because ink art has to be flipped for a light row.
    // The lock is different from the rest: it never comes from a title and
    // never from the torrent's file list, only from a completed level-2 walk of
    // an archive index that found password-protected entries.
    enum class ContentBadge {
        Archive = 0,      // compressed.png
        Encrypted,        // encrypted.png, archive entries that need a password
        DiscImage,        // iso.png (.iso/.dmg/.img)
        Video,            // video.png, only from the real file list
        NativeExecutable, // executable.png, native Windows/Linux binaries
        Count
    };
    struct BadgeArt {
        Fl_Image* confirmed = nullptr;
        Fl_Image* hint = nullptr;
    };
    Fl_Image* badgeIcon(ContentBadge badge, bool hinted);
    void clearBadgeIcons();
    BadgeArt m_badgeArt[static_cast<int>(ContentBadge::Count)];
    bool m_darkMode = false;

    // Level 2 under an expanded row: one clickable line that reads the archive
    // index from its first and last piece. 'top'/'bottom' are widget
    // coordinates, recorded by the draw so the click can be matched.
    struct ArchiveAction {
        int fileIndex = -1;
        std::string path;
        std::string magnet;
        std::string torrentUrl;
        std::string line; // result, empty until asked
        bool running = false;
        bool visible = false;
        // How many entries the last successful walk found password-protected.
        // Drives the lock badge; 0 until a check completes.
        int encrypted = 0;
        // Every entry the last successful walk listed, in archive order. Kept
        // so the row can show the contents without asking the swarm again;
        // empty until a check completes.
        std::vector<SecurityFile> entries;
        // The .torrent bytes the row's file list was read from. When present,
        // the check reads its two pieces straight away instead of exchanging
        // metadata with the swarm a second time.
        std::string torrentBody;
        int top = 0;
        int bottom = 0;
    };
    std::unordered_map<std::string, ArchiveAction> m_archive;
    // The archive checks currently running, so the destructor can stop them
    // instead of leaving them to run out their deadlines. Keyed like
    // m_archive: one check per row at a time.
    std::unordered_map<std::string, std::shared_ptr<ArchiveCheckTask>>
        m_archiveTasks;
    int archiveIndexFor(const std::vector<TorrentFileEntry>& files) const;
    // Password-protected entries found by the last archive check for this row,
    // 0 when none was run or the index held none.
    int encryptedForRow(int row);
    // Lines the entry list under the archive answer takes, including the
    // "... and N more" line. Used by both the draw and the height so the row
    // can never reserve a different number of lines from the one it paints.
    static int archiveEntryRows(const ArchiveAction& action);
    void startArchiveCheck(const std::string& key);
    bool archiveLineHit(int row, int x, int y);

    // Cache key for a result: infohash, else the .torrent URL, else the row.
    static std::string cacheKey(const SearchResult& r);
    // A fetch landed: only the rows showing that same torrent change.
    void filesArrived(const std::string& key);
    // Widens a row so its file list fits underneath the name.
    void updateRowHeight(int row);
    bool isExpanded(const std::string& key) const;
    void setExpanded(const std::string& key, bool on);
    void forEachRow(const std::function<void(int, const SearchResult&)>& fn);

    void initializeColumns();
    void layoutColumns();
    void updateSortedIndices();
    void drawHeader(int col, int x, int y, int w, int h);
    void drawCell(int row, int col, int x, int y, int w, int h);
    void drawExpandCell(int row, int x, int y, int w, int h);
    void drawFileList(int row, int x, int y, int w, int h);
    bool compareResults(int idx1, int idx2) const;
};

#endif // SEARCHRESULTSWIDGET_H