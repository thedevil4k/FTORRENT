#ifndef ARCHIVECONTENTSDIALOG_H
#define ARCHIVECONTENTSDIALOG_H

#include "ArchiveInspector.h"

#include <FL/Fl_Window.H>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

class Fl_Box;
class Fl_Browser;
class Fl_Button;

/**
 * @brief Lists the .exe files inside a RAR/ZIP and hashes the one the user picks.
 *
 * The archive is only read. Nothing is extracted to disk and nothing is
 * executed: ArchiveInspector streams the selected entry through SHA-256, and the
 * finished hash opens the VirusTotal report in the browser. The dialog stays
 * modal so the archive cannot be deleted from under a running worker by another
 * FTorrent window.
 */
class ArchiveContentsDialog : public Fl_Window {
public:
    /// @param archivePath        File on disk, already resolved by the caller.
    /// @param archiveDisplayPath Path to show in the title (normally the torrent's).
    ArchiveContentsDialog(const std::string& archivePath,
                          const std::string& archiveDisplayPath);
    ~ArchiveContentsDialog() override;

    void show_modal();

private:
    void startListing();
    void startHash(int browserIndex);
    void updateControls();
    void handleListing(ArchiveInspector::Listing listing);
    void handleHashFinished(bool ok,
                            const std::string& entryPath,
                            const std::string& hash,
                            const std::string& error);
    void setStatus(const std::string& text);

    std::string m_archivePath;
    std::vector<ArchiveInspector::Entry> m_entries;

    Fl_Browser* m_browser = nullptr;
    Fl_Button* m_virusTotalButton = nullptr;
    Fl_Button* m_closeButton = nullptr;
    Fl_Box* m_status = nullptr;

    bool m_listing = false;
    bool m_hashing = false;
    bool m_listed = false;
    bool m_statusSticky = false;
    std::shared_ptr<std::atomic<bool>> m_cancel;

    static void onSelect(Fl_Widget* w, void* data);
    static void onVirusTotal(Fl_Widget* w, void* data);
    static void onClose(Fl_Widget* w, void* data);
    static void deliverListing(void* data);
    static void deliverHash(void* data);
};

#endif // ARCHIVECONTENTSDIALOG_H
