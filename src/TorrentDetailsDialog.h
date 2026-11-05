#ifndef TORRENTDETAILSDIALOG_H
#define TORRENTDETAILSDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>
#include <memory>
#include <vector>
#include "TorrentItem.h"
#include "SecurityBadge.h"
#include "SecurityReputation.h"

class ArchiveCheckTask;

/**
 * @brief Diálogo de detalles de un torrent específico
 * 
 * Muestra información detallada en pestañas:
 * - General: Información básica del torrent
 * - Trackers: Lista de trackers
 * - Peers: Peers conectados
 * - Security: Análisis de los metadatos de los archivos
 * - Files: Archivos del torrent (si está disponible)
 *
 * La comprobación de seguridad vive ahora en el análisis previo a la
 * descarga; este diálogo ya no ofrece botones que actúen sobre archivos
 * completos.
 */
class TorrentDetailsDialog : public Fl_Window {
public:
    TorrentDetailsDialog(TorrentItem* torrent);
    ~TorrentDetailsDialog();
    
    void show_modal();
    void updateInfo();

    // Opens on the Security tab and asks about the torrent's hash, for the
    // shield click in the torrent list.
    void showSecurityAndCheck();
    
private:
    TorrentItem* m_torrent;
    
    // Tabs
    Fl_Tabs* m_tabs;
    
    // General tab
    Fl_Group* m_generalTab;
    SecurityShieldBox* m_generalShield;
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
    Fl_Box* m_generalSecuritySummary;
    
    // Trackers tab
    Fl_Group* m_trackersTab;
    Fl_Text_Display* m_trackersDisplay;
    Fl_Text_Buffer* m_trackersBuffer = nullptr;
    
    // Peers tab
    Fl_Group* m_peersTab;
    Fl_Text_Display* m_peersDisplay;
    Fl_Text_Buffer* m_peersBuffer = nullptr;
    
    // Security tab
    Fl_Group* m_securityTab;
    SecurityShieldBox* m_securityShield;
    Fl_Box* m_securitySummary;
    Fl_Text_Display* m_securityDisplay;
    Fl_Text_Buffer* m_securityBuffer = nullptr;
    Fl_Button* m_checkArchiveButton = nullptr;
    SecurityReport m_securityReport;
    bool m_securityLoaded = false;
    // Level 2: the answer from reading the archive's two ends.
    std::string m_archiveLine;
    bool m_archiveRunning = false;
    // Every entry that answer listed, in archive order. Names and sizes only:
    // the check reads headers, never file data.
    std::vector<SecurityFile> m_archiveEntries;
    // The check currently running, so the destructor can stop it. Null when
    // nothing is being read.
    std::shared_ptr<ArchiveCheckTask> m_archiveTask;
    // Level 3 fallback: a file that is already complete on disk is hashed
    // locally, never uploaded, and only after the user asked.
    bool m_hashingLocally = false;
    std::string m_reputationNote;
    std::shared_ptr<std::atomic<bool>> m_hashCancel;
    // Reputation of the hash the user clicked the shield about. Never fetched
    // automatically; see checkReputation().
    ReputationResult m_reputation;
    std::string m_reputationSha;
    // Cleared in the destructor, so a lookup landing later cannot touch a dead
    // dialog.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    // Files tab
    Fl_Group* m_filesTab;
    Fl_Browser* m_filesBrowser;
    std::vector<TorrentItem::FileInfo> m_files;
    bool m_filesLoaded = false;

    // Buttons
    Fl_Button* m_refreshButton;
    Fl_Button* m_closeButton;
    
    // UI Creation
    void createGeneralTab();
    void createTrackersTab();
    void createPeersTab();
    void createSecurityTab();
    void createFilesTab();
    void createButtons();
    
    // Update methods
    void updateGeneralInfo();
    void updateTrackers();
    void updatePeers();
    void updateSecurity();
    void checkReputation();
    void checkArchive();
    void requestReputation(const std::string& sha256);
    void startLocalHash(const std::string& relativePath);
    void onLocalHashDone(const std::string& relativePath, bool ok,
                         const std::string& digestOrError);
    static void onCheckArchiveClicked(Fl_Widget* w, void* data);
    void updateFiles();
    
    // Callbacks
    static void onRefresh(Fl_Widget* w, void* data);
    static void onClose(Fl_Widget* w, void* data);
    static void updateTimerCallback(void* data);
    
    // Helper
    std::string formatLabel(const std::string& label, const std::string& value);
};

#endif // TORRENTDETAILSDIALOG_H
