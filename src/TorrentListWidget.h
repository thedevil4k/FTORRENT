#ifndef TORRENTLISTWIDGET_H
#define TORRENTLISTWIDGET_H

#include <FL/Fl_Table_Row.H>
#include <vector>
#include <string>
#include <functional>
#include "TorrentItem.h"

/**
 * @brief Widget de tabla para mostrar la lista de torrents
 * 
 * Muestra torrents en formato tabla con columnas:
 * - Nombre
 * - Tamaño
 * - Progreso
 * - Estado
 * - Download Speed
 * - Upload Speed
 * - ETA
 * - Ratio
 * - Peers
 */
class TorrentListWidget : public Fl_Table_Row {
public:
    enum Column {
        COL_NAME = 0,
        COL_SIZE,
        COL_PROGRESS,
        COL_STATUS,
        COL_DOWN_SPEED,
        COL_UP_SPEED,
        COL_ETA,
        COL_RATIO,
        COL_PEERS,
        COL_COUNT
    };

    TorrentListWidget(int x, int y, int w, int h, const char* label = nullptr);
    ~TorrentListWidget();

    // Data management
    void setTorrents(const std::vector<TorrentItem*>& torrents);
    void addTorrent(TorrentItem* torrent);
    void removeTorrent(const std::string& hash);
    void updateTorrent(TorrentItem* torrent);
    void clear();
    
    // Selection
    std::vector<TorrentItem*> getSelectedTorrents();
    TorrentItem* getSelectedTorrent();
    bool hasSelection();
    
    // Sorting
    void sortBy(Column column, bool ascending = true);
    
    // Drag-and-drop
    void setOnDropCallback(std::function<void(const std::string&)> cb) { m_onDropCallback = cb; }
    
    // Context menu
    void showContextMenu();
    
    // Details
    void showDetailsDialog(TorrentItem* torrent);
    
protected:
    // Fl_Table overrides
    void draw_cell(TableContext context, int row, int col, 
                   int x, int y, int w, int h) override;
    
    int handle(int event) override;
    void draw() override;
    
private:
    std::vector<TorrentItem*> m_torrents;
    std::vector<int> m_sortedIndices;
    
    Column m_sortColumn;
    bool m_sortAscending;
    
    // Drag-and-drop state
    bool m_dropHighlight;
    std::function<void(const std::string&)> m_onDropCallback;
    
    // Column info
    struct ColumnInfo {
        const char* name;
        int width;
        Fl_Align alignment;
    };
    
    static const ColumnInfo COLUMN_INFO[COL_COUNT];
    
    // Helper methods
    void initializeColumns();
    void updateSortedIndices();
    TorrentItem* getTorrentAt(int row) const;
    int findTorrentRow(const std::string& hash) const;
    
    // Drawing helpers
    void drawHeader(int col, int x, int y, int w, int h);
    void drawCell(int row, int col, int x, int y, int w, int h);
    void drawProgressBar(double progress, int x, int y, int w, int h);
    void drawDropOverlay();
    
    // Sorting comparator
    bool compareTorrents(int idx1, int idx2) const;
    
    // Colors
    static constexpr Fl_Color COLOR_HEADER_BG = FL_LIGHT2;
    Fl_Color getRowColor(int row) const;
    Fl_Color getTextColor(int row) const;
    Fl_Color getProgressBgColor() const;
    Fl_Color getProgressFgColor() const;
};

#endif // TORRENTLISTWIDGET_H
