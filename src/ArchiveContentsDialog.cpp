#include "ArchiveContentsDialog.h"
#include "SystemUtils.h"
#include "VirusTotal.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>

#include <exception>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

namespace {

std::string fileNameOf(const std::string& path) {
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

// Archive entry names are attacker-controlled. Control characters could break
// a row into what looks like several entries, so they are replaced before the
// name reaches a label. The selection itself is by entry index, never by name.
std::string displayText(const std::string& text) {
    std::string out = text;
    for (char& ch : out) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte == 0x7f) ch = '?';
    }
    return out;
}

std::string formatSize(std::uint64_t bytes) {
    static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream text;
    text << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value
         << ' ' << units[unit];
    return text.str();
}

// A worker's result. Closing the dialog shares the cancellation flag with the
// worker; deliverListing()/deliverHash() drop the result without touching a
// possibly deleted dialog when that flag is set.
struct ListingDelivery {
    std::shared_ptr<std::atomic<bool>> cancel;
    ArchiveContentsDialog* target = nullptr;
    ArchiveInspector::Listing listing;
};

struct HashDelivery {
    std::shared_ptr<std::atomic<bool>> cancel;
    ArchiveContentsDialog* target = nullptr;
    bool ok = false;
    std::string entryPath;
    std::string hash;
    std::string error;
};

} // namespace

ArchiveContentsDialog::ArchiveContentsDialog(const std::string& archivePath,
                                             const std::string& archiveDisplayPath)
    : Fl_Window(640, 460)
    , m_archivePath(archivePath)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    std::string title = "Archive Contents";
    const std::string name = displayText(fileNameOf(archiveDisplayPath));
    if (!name.empty()) title += " - " + name;
    copy_label(title.c_str());

    Fl_Box* hint = new Fl_Box(20, 12, 600, 24,
                              "Read-only: select an .exe to hash. Nothing is extracted or executed.");
    hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    hint->labelsize(11);

    m_browser = new Fl_Browser(20, 40, 600, 300);
    m_browser->type(FL_HOLD_BROWSER);
    m_browser->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
    m_browser->textfont(FL_COURIER);
    m_browser->textsize(11);
    m_browser->callback(onSelect, this);
    m_browser->copy_tooltip(
        ("Archive: " + displayText(archiveDisplayPath.empty() ? archivePath : archiveDisplayPath)).c_str());

    m_status = new Fl_Box(20, 348, 600, 56);
    m_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    m_status->labelsize(11);

    m_virusTotalButton = new Fl_Button(20, 415, 190, 30, "Open in VirusTotal");
    m_virusTotalButton->callback(onVirusTotal, this);
    m_virusTotalButton->tooltip(
        "Decompress the selected .exe in memory, compute its SHA-256 and open the "
        "VirusTotal report. The file is not uploaded; the hash is sent to "
        "VirusTotal when the report opens.");
    m_virusTotalButton->deactivate();

    m_closeButton = new Fl_Button(540, 415, 80, 30, "Close");
    m_closeButton->callback(onClose, this);

    end();
    set_modal();

    startListing();
}

ArchiveContentsDialog::~ArchiveContentsDialog() {
    // A listing or hash worker may still be reading. Ask it to stop; any result
    // it posts later carries this same flag and is dropped before it can touch
    // this dialog.
    if (m_cancel) m_cancel->store(true);
}

void ArchiveContentsDialog::show_modal() {
    show();
    while (shown()) {
        Fl::wait();
    }
}

void ArchiveContentsDialog::startListing() {
    m_listing = true;
    m_listed = false;
    m_statusSticky = false;
    m_status->copy_tooltip(nullptr);
    updateControls();

    auto cancel = m_cancel;
    const std::string archivePath = m_archivePath;

    try {
        std::thread([cancel, this, archivePath]() {
            ArchiveInspector::Listing listing;
            try {
                listing = ArchiveInspector::listExecutables(archivePath, cancel.get());
            } catch (const std::exception& e) {
                listing.ok = false;
                listing.error = e.what();
            } catch (...) {
                listing.ok = false;
                listing.error = "unexpected error";
            }

            if (cancel->load()) {
                return;
            }

            auto* delivery = new ListingDelivery;
            delivery->cancel = cancel;
            delivery->target = this;
            delivery->listing = std::move(listing);
            Fl::awake(&ArchiveContentsDialog::deliverListing, delivery);
        }).detach();
    } catch (const std::exception& e) {
        m_listing = false;
        m_listed = true;
        m_statusSticky = true;
        setStatus(std::string("Could not start the archive reader: ") + e.what());
        updateControls();
    }
}

void ArchiveContentsDialog::startHash(int browserIndex) {
    if (m_listing || m_hashing) {
        return;
    }
    if (browserIndex <= 0 || browserIndex > static_cast<int>(m_entries.size())) {
        m_statusSticky = true;
        setStatus("Select an .exe file inside the archive first.");
        return;
    }

    const ArchiveInspector::Entry entry = m_entries[browserIndex - 1];
    if (entry.encrypted) {
        m_statusSticky = true;
        setStatus(
            "This entry is password-protected. FTorrent does not ask for archive passwords.");
        return;
    }

    m_hashing = true;
    m_statusSticky = false;
    m_status->copy_tooltip(nullptr);
    setStatus("Computing the SHA-256 of " + entry.path +
              " from the archive. The entry stays in memory...");
    updateControls();

    auto cancel = m_cancel;
    const std::string archivePath = m_archivePath;
    const std::string entryPath = entry.path;
    const int entryIndex = entry.index;

    try {
        std::thread([cancel, this, archivePath, entryPath, entryIndex]() {
            std::string hash;
            std::string error;
            bool ok = false;
            try {
                ok = ArchiveInspector::hashExecutable(
                    archivePath, entryIndex, cancel.get(), hash, error);
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "unexpected error";
            }

            if (cancel->load()) {
                return;
            }

            auto* delivery = new HashDelivery;
            delivery->cancel = cancel;
            delivery->target = this;
            delivery->ok = ok;
            delivery->entryPath = entryPath;
            delivery->hash = std::move(hash);
            delivery->error = std::move(error);
            Fl::awake(&ArchiveContentsDialog::deliverHash, delivery);
        }).detach();
    } catch (const std::exception& e) {
        m_hashing = false;
        m_statusSticky = true;
        setStatus(std::string("Could not start the hash worker: ") + e.what());
        updateControls();
    }
}

void ArchiveContentsDialog::updateControls() {
    if (!m_virusTotalButton || !m_status) {
        return;
    }

    if (m_listing) {
        m_virusTotalButton->deactivate();
        if (!m_statusSticky) {
            setStatus("Reading the archive contents. Nothing is extracted or executed.");
        }
        return;
    }

    if (m_hashing) {
        m_virusTotalButton->deactivate();
        return;
    }

    const int value = m_browser ? m_browser->value() : 0;
    if (value <= 0 || value > static_cast<int>(m_entries.size())) {
        m_virusTotalButton->deactivate();
        if (!m_statusSticky) {
            setStatus(m_listed ? "Select an .exe file inside the archive."
                               : "Reading the archive contents.");
        }
        return;
    }

    const ArchiveInspector::Entry& entry = m_entries[value - 1];
    if (entry.encrypted) {
        m_virusTotalButton->deactivate();
        if (!m_statusSticky) {
            setStatus(
                "This entry is password-protected. FTorrent does not ask for archive passwords.");
        }
        return;
    }

    m_virusTotalButton->activate();
    if (!m_statusSticky) {
        setStatus(
            "SHA-256 is computed locally. Opening the report sends the hash to "
            "VirusTotal; the .exe is not uploaded.");
    }
}

void ArchiveContentsDialog::handleListing(ArchiveInspector::Listing listing) {
    m_listing = false;
    m_listed = true;
    m_entries.clear();
    m_browser->clear();

    // A failed scan can leave a partial list behind. Showing it would invite a
    // selection from an archive the reader already rejected, so only a complete
    // scan reaches the browser.
    if (!listing.ok) {
        m_statusSticky = true;
        setStatus("Could not read the archive: " + listing.error);
        updateControls();
        return;
    }

    m_entries = std::move(listing.executables);
    for (const ArchiveInspector::Entry& entry : m_entries) {
        std::ostringstream row;
        row << displayText(entry.path) << "  |  ";
        if (entry.sizeKnown) {
            row << formatSize(entry.size);
        } else {
            row << "size unknown";
        }
        if (entry.encrypted) {
            row << "  |  [password-protected]";
        }
        m_browser->add(row.str().c_str());
    }

    if (m_entries.empty()) {
        m_statusSticky = true;
        setStatus("No .exe files were found in this archive. Nothing was extracted.");
        updateControls();
        return;
    }

    m_statusSticky = false;
    m_browser->value(1);
    updateControls();
}

void ArchiveContentsDialog::handleHashFinished(bool ok,
                                                const std::string& entryPath,
                                                const std::string& hash,
                                                const std::string& error) {
    m_hashing = false;
    m_statusSticky = false;
    updateControls();

    if (!ok) {
        m_statusSticky = true;
        setStatus("Could not read " + entryPath + " from the archive: " + error);
        return;
    }

    const std::string url = VirusTotal::fileReportUrl(hash);
    if (url.empty()) {
        m_statusSticky = true;
        setStatus("Could not build a valid VirusTotal link.");
        return;
    }

    m_statusSticky = true;
    setStatus(
        "SHA-256 computed. Opening the VirusTotal report; FTorrent does not read the verdict.");
    m_status->copy_tooltip(hash.c_str());
    SystemUtils::openUrl(url);
}

void ArchiveContentsDialog::setStatus(const std::string& text) {
    if (m_status) {
        m_status->copy_tooltip(nullptr);
        m_status->copy_label(displayText(text).c_str());
    }
}

void ArchiveContentsDialog::onSelect(Fl_Widget* w, void* data) {
    (void)w;
    ArchiveContentsDialog* dlg = static_cast<ArchiveContentsDialog*>(data);
    dlg->m_statusSticky = false;
    dlg->updateControls();
}

void ArchiveContentsDialog::onVirusTotal(Fl_Widget* w, void* data) {
    (void)w;
    ArchiveContentsDialog* dlg = static_cast<ArchiveContentsDialog*>(data);
    dlg->startHash(dlg->m_browser->value());
}

void ArchiveContentsDialog::onClose(Fl_Widget* w, void* data) {
    (void)w;
    ArchiveContentsDialog* dlg = static_cast<ArchiveContentsDialog*>(data);
    dlg->hide();
}

void ArchiveContentsDialog::deliverListing(void* data) {
    std::unique_ptr<ListingDelivery> delivery(static_cast<ListingDelivery*>(data));
    if (!delivery || !delivery->cancel || delivery->cancel->load() || !delivery->target) {
        return;
    }
    delivery->target->handleListing(std::move(delivery->listing));
}

void ArchiveContentsDialog::deliverHash(void* data) {
    std::unique_ptr<HashDelivery> delivery(static_cast<HashDelivery*>(data));
    if (!delivery || !delivery->cancel || delivery->cancel->load() || !delivery->target) {
        return;
    }
    delivery->target->handleHashFinished(
        delivery->ok, delivery->entryPath, delivery->hash, delivery->error);
}
