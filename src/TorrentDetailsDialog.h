#ifndef TORRENTDETAILSDIALOG_H
#define TORRENTDETAILSDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Button.H>
#include "TorrentItem.h"

/**
 * @brief Diálogo de detalles de un torrent específico
 * 
 * Muestra información detallada en pestañas:
 * - General: Información básica del torrent
 * - Trackers: Lista de trackers
 * - Peers: Peers conectados
 * - Files: Archivos del torrent (si está disponible)
 */
class TorrentDetailsDialog : public Fl_Window {
public:
    TorrentDetailsDialog(TorrentItem* torrent);
    ~TorrentDetailsDialog();
    
    void show_modal();
    void updateInfo();
    
private:
    TorrentItem* m_torrent;
    
    // Tabs
    Fl_Tabs* m_tabs;
    
    // General tab
    Fl_Group* m_generalTab;
    Fl_Box* m_nameLabel;
    Fl_Box* m_sizeLabel;
    Fl_Box* m_hashLabel;
    Fl_Box* m_statusLabel;
    Fl_Box* m_downloadedLabel;
    Fl_Box* m_uploadedLabel;
    Fl_Box* m_ratioLabel;
    Fl_Box* m_peersLabel;
    Fl_Box* m_seedsLabel;
    Fl_Box* m_downloadSpeedLabel;
    Fl_Box* m_uploadSpeedLabel;
    Fl_Box* m_etaLabel;
    Fl_Box* m_savePathLabel;
    
    // Trackers tab
    Fl_Group* m_trackersTab;
    Fl_Text_Display* m_trackersDisplay;
    Fl_Text_Buffer* m_trackersBuffer;
    
    // Peers tab
    Fl_Group* m_peersTab;
    Fl_Text_Display* m_peersDisplay;
    Fl_Text_Buffer* m_peersBuffer;
    
    // Files tab
    Fl_Group* m_filesTab;
    Fl_Text_Display* m_filesDisplay;
    Fl_Text_Buffer* m_filesBuffer;
    
    // Buttons
    Fl_Button* m_refreshButton;
    Fl_Button* m_closeButton;
    
    // UI Creation
    void createGeneralTab();
    void createTrackersTab();
    void createPeersTab();
    void createFilesTab();
    void createButtons();
    
    // Update methods
    void updateGeneralInfo();
    void updateTrackers();
    void updatePeers();
    void updateFiles();
    
    // Callbacks
    static void onRefresh(Fl_Widget* w, void* data);
    static void onClose(Fl_Widget* w, void* data);
    static void updateTimerCallback(void* data);
    
    // Helper
    std::string formatLabel(const std::string& label, const std::string& value);
};

#endif // TORRENTDETAILSDIALOG_H
