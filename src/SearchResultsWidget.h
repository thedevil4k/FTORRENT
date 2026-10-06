#ifndef SEARCHRESULTSWIDGET_H
#define SEARCHRESULTSWIDGET_H

#include <FL/Fl_Table_Row.H>
#include "SearchEngine.h"
#include "FileListService.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
        COL_EXPAND = 0,   // narrow, no header text: the file-list toggle
        COL_NAME,
        COL_SIZE,
        COL_SEEDERS,
        COL_LEECHERS,
        COL_SOURCE,
        COL_COUNT
    };

    SearchResultsWidget(int x, int y, int w, int h, const char* label = nullptr);
    ~SearchResultsWidget() override;

    void setResults(const std::vector<SearchResult>& results);

    // Adds rows without re-sorting the whole table: used while results stream in
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
    using DownloadCallback =
        std::function<void(const SearchResult&, const std::vector<TorrentFileEntry>&)>;
    void setOnDownloadCallback(DownloadCallback cb) { m_onDownload = cb; }

protected:
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
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