#ifndef ADDTORRENTDIALOG_H
#define ADDTORRENTDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Box.H>
#include "TorrentFileList.h" // TorrentFileEntry, for pre-filling magnets
#include "SecurityBadge.h"
#include "SecurityReputation.h"
#include <libtorrent/add_torrent_params.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ArchiveCheckTask;
class AddTorrentDialogReputation;
class TorrentFileHashTask;

/**
 * @brief Diálogo avanzado para añadir torrents
 *
 * Permite:
 * - Seleccionar archivo .torrent o ingresar magnet link
 * - Elegir ruta de descarga
 * - Opción de iniciar inmediatamente o pausado
 * - Seleccionar prioridad
 */
class AddTorrentDialog : public Fl_Window {
public:
    enum class Priority { Normal = 0, High = 1, Low = 2 };

    AddTorrentDialog();
    ~AddTorrentDialog();

    // Show dialog and return true if OK was clicked
    bool show_modal();

    // Get results
    std::string getTorrentPath() const { return m_torrentPath; }
    std::string getMagnetLink() const { return m_magnetLink; }
    std::string getSavePath() const { return m_savePath; }
    bool getStartImmediately() const { return m_startImmediatelyResult; }
    Priority getPriority() const { return m_priority; }
    std::vector<int> getFilePriorities() const { return m_filePriorities; }
    bool isMagnet() const { return !m_magnetLink.empty(); }

    // Setters for pre-filling
    void setTorrentPath(const std::string& path);
    void setMagnetLink(const std::string& magnet);
    void setSavePath(const std::string& path);
    // Shows the already-known files of a magnet (from the search row) with
    // every box checked, so the user unchecks what to skip. Same order as
    // getFilePriorities() returns, and as libtorrent expects them.
    void setFileList(const std::vector<TorrentFileEntry>& files);
    // The .torrent link and bytes the search row already resolved. Either one
    // lets Check archive read its two pieces without a second metadata
    // exchange with the swarm; without them it falls back to the magnet.
    void setTorrentUrl(const std::string& url) { m_torrentUrl = url; }
    void setTorrentBody(const std::string& body) { m_torrentBody = body; }

private:
    friend class AddTorrentDialogReputation;

    // Common widgets
    Fl_Input* m_savePathInput;
    Fl_Button* m_browseSavePath;
    Fl_Check_Button* m_startImmediately;
    Fl_Choice* m_priorityChoice;

    // File-specific widgets
    Fl_Input* m_filePathInput;
    Fl_Button* m_browseFile;

    // Magnet-specific widgets
    Fl_Input* m_magnetInput;
    // Green dot drawn at the right end of the magnet field while this import
    // is the one going through a magnet link. Shown or hidden as the fields
    // change; it carries no state of its own.
    Fl_Box* m_magnetOriginDot;

    // Files display
    Fl_Check_Browser* m_fileBrowser;
    Fl_Button* m_selectNonExecutables;
    Fl_Box* m_securityPanel;
    SecurityShieldBox* m_securityShield;
    Fl_Box* m_securitySummary;
    Fl_Box* m_securityDetail;
    Fl_Box* m_securityHint;
    Fl_Box* m_reputationLabel;
    Fl_Button* m_checkArchive;
    // What the last archive check listed, one line per entry. Hidden until a
    // check answers, so the dialog stays compact while nothing is known.
    Fl_Hold_Browser* m_archiveBrowser = nullptr;
    // Result of the last extra check, whichever ran last. Both are explicit
    // user actions and share one line so the dialog stays compact.
    std::string m_checkLine;
    bool m_archiveRunning = false;
    // Where the check can read metadata without touching the swarm: the bytes
    // the search row already resolved, or the .torrent link it came from.
    std::string m_torrentBody;
    std::string m_torrentUrl;
    // The whole-file SHA-256 the selection's metadata carries, and the file it
    // belongs to. Shown in the small print whether or not a provider is asked.
    std::string m_torrentSha256;
    std::string m_torrentSha256Path;
    std::vector<SecurityFile> m_archiveEntries;
    void showArchiveEntries();

    // Buttons
    Fl_Button* m_okButton;
    Fl_Button* m_cancelButton;

    // Results
    std::string m_torrentPath;
    std::string m_magnetLink;
    std::string m_savePath;
    std::vector<int> m_filePriorities;
    bool m_startImmediatelyResult;
    Priority m_priority;
    bool m_okClicked;

    // Files currently shown in the browser. The vector is indexed by browser
    // row (row 1 is m_files[0]); fileIndex is the libtorrent index and may
    // contain gaps for pad files.
    std::vector<TorrentFileEntry> m_files;
    int m_totalFileCount = 0;
    SecurityReport m_selectedReport;
    // Reputation of the hash the user asked about. Never starts on its own:
    // only a click on the shield runs checkReputation().
    ReputationResult m_reputation;
    std::string m_reputationSha;
    // Cleared in the destructor so a lookup that lands later cannot touch a
    // dialog that is already gone.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    // The archive check currently running, so the destructor can stop it.
    // Null when nothing is being read.
    std::shared_ptr<ArchiveCheckTask> m_archiveTask;
    // The on-demand file hash currently running, so the destructor can stop it.
    // Null when nothing is being hashed.
    std::shared_ptr<TorrentFileHashTask> m_fileHashTask;
    // SHA-256 computed on demand from a selected file's pieces, and the file
    // it belongs to. Shown in the small print once available.
    std::string m_fileHashSha256;
    std::string m_fileHashPath;
    bool m_fileHashRunning = false;
    // Reputation logic (hash lookup, provider queries) kept in a separate file
    // so AddTorrentDialog.cpp stays under the source-size limit.
    std::unique_ptr<AddTorrentDialogReputation> m_reputationHelper;

    // UI Creation
    void createUI();
    void createButtons();

    // Validation
    bool validate();

    // Callbacks
    static void onBrowseFile(Fl_Widget* w, void* data);
    static void onBrowseSavePath(Fl_Widget* w, void* data);
    static void onOK(Fl_Widget* w, void* data);
    static void onCancel(Fl_Widget* w, void* data);
    static void onMagnetFieldChanged(Fl_Widget* w, void* data);
    static void onFileSelectionChanged(Fl_Widget* w, void* data);
    static void onSelectNonExecutables(Fl_Widget* w, void* data);

    // Helper
    void loadDefaults();
    void updateFileList(const std::string& torrentPath);
    // Fills the browser and the security panel from a resolved list. The
    // total count includes pad files, because file priorities are indexed by
    // the libtorrent file index, not by visible row.
    void showFiles(const std::vector<TorrentFileEntry>& files,
                   int totalFileCount);
    std::vector<TorrentFileEntry> selectedFiles() const;
    void updateSecurityPanel();
    void checkReputation();
    std::optional<lt::add_torrent_params> buildAddTorrentParams();
    void checkArchive();
    static void onCheckArchive(Fl_Widget* w, void* data);
    bool confirmRiskySelection();
    std::string fileRowLabel(const TorrentFileEntry& file) const;
    // Shows or hides the green dot, following the same precedence validate()
    // uses: a .torrent file wins over a magnet, so the dot only shows while
    // the magnet is what would actually be imported.
    void updateOriginDot();
};

#endif // ADDTORRENTDIALOG_H
