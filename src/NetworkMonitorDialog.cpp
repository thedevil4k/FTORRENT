#include "NetworkMonitorDialog.h"
#include "SecurityEvents.h"
#include "TorrentItem.h"
#include "Format.h"
#include "TorrentManager.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string clockOf(std::int64_t when) {
    std::time_t seconds = static_cast<std::time_t>(when);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%H:%M:%S", &local);
    return buffer;
}

// One monospaced line per connection. Fixed widths, not columns, so the list
// stays readable in a plain Fl_Browser without a second table implementation.
std::string connectionLine(const std::string& torrent,
                           const TorrentItem::PeerInfo& peer) {
    std::ostringstream out;
    out << std::left << std::setw(18) << torrent.substr(0, 18) << std::setw(16)
        << peer.ip << std::right << std::setw(6) << peer.port << "  "
        << std::left << std::setw(4) << (peer.outgoing ? "out" : "in")
        << std::setw(4) << (peer.encrypted ? "yes" : "no") << std::setw(10)
        << Format::perSecond(peer.downloadRate) << "  " << std::setw(10)
        << Format::perSecond(peer.uploadRate) << "  " << std::right
        << std::setw(5) << static_cast<int>(peer.progress * 100.0) << "%  "
        << std::left << peer.client;
    return out.str();
}

std::string eventLine(const SecurityEvent& event) {
    std::string line = clockOf(event.when) + "  [" + event.kind + "] ";
    if (!event.torrent.empty()) line += event.torrent + ": ";
    line += event.detail;
    // A remote sentence may contain anything; one row per event.
    for (char& ch : line) {
        if (static_cast<unsigned char>(ch) < 0x20) ch = ' ';
    }
    return line;
}

} // namespace

NetworkMonitorDialog::NetworkMonitorDialog(TorrentManager* manager)
    : Fl_Window(760, 460, "Network Monitor")
    , m_manager(manager) {
    createUI();
    end();
    set_modal();

    refreshConnections();
    refreshEvents();
    Fl::add_timeout(1.0, onTimer, this);
}

NetworkMonitorDialog::~NetworkMonitorDialog() {
    Fl::remove_timeout(onTimer, this);
}

void NetworkMonitorDialog::createUI() {
    m_tabs = new Fl_Tabs(10, 10, 740, 400);

    // --- Connections ---
    m_connectionsTab = new Fl_Group(10, 35, 740, 375, "Connections");
    m_connectionsTab->begin();

    m_summary = new Fl_Box(20, 42, 720, 20);
    m_summary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_summary->labelsize(11);

    m_connections = new Fl_Browser(20, 64, 720, 336);
    m_connections->textfont(FL_COURIER);
    m_connections->textsize(11);
    m_connections->has_scrollbar(Fl_Browser_::VERTICAL);
    m_connectionsTab->end();

    // --- Events ---
    m_eventsTab = new Fl_Group(10, 35, 740, 375, "Events");
    m_eventsTab->hide();
    m_eventsTab->begin();

    m_eventSummary = new Fl_Box(20, 42, 720, 20);
    m_eventSummary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_eventSummary->labelsize(11);
    m_eventSummary->copy_label(
        "Peer bans, invalid requests, torrent and tracker errors reported by "
        "the engine. Newest last.");

    m_eventList = new Fl_Browser(20, 64, 720, 336);
    m_eventList->textfont(FL_COURIER);
    m_eventList->textsize(11);
    m_eventList->has_scrollbar(Fl_Browser_::VERTICAL);
    m_eventsTab->end();

    m_tabs->end();

    m_refreshButton = new Fl_Button(400, 420, 100, 28, "Refresh");
    m_refreshButton->callback(onRefresh, this);

    m_clearButton = new Fl_Button(510, 420, 110, 28, "Clear events");
    m_clearButton->callback(onClear, this);

    m_closeButton = new Fl_Button(630, 420, 100, 28, "Close");
    m_closeButton->callback(onClose, this);
}

void NetworkMonitorDialog::refreshConnections() {
    if (!m_connections) return;

    const int scroll = m_connections->value();
    m_connections->clear();

    int torrents = 0;
    int links = 0;
    int encrypted = 0;
    int outgoing = 0;

    if (m_manager) {
        for (TorrentItem* item : m_manager->getAllTorrents()) {
            if (!item) continue;
            ++torrents;
            const std::string name = item->getName();
            for (const TorrentItem::PeerInfo& peer : item->getPeers()) {
                ++links;
                if (peer.encrypted) ++encrypted;
                if (peer.outgoing) ++outgoing;
                m_connections->add(connectionLine(name, peer).c_str());
            }
        }
    }

    std::ostringstream summary;
    summary << links << " connection(s) across " << torrents << " torrent(s), "
            << outgoing << " outgoing, " << encrypted
            << " encrypted. This shows the peers FTorrent talks to, not packet "
               "contents.";
    if (m_summary) m_summary->copy_label(summary.str().c_str());

    if (links == 0) {
        m_connections->add(
            "No connections right now. A paused torrent, or one with no peers, "
            "has none.");
    }
    if (scroll > 0 && links > 0) m_connections->value(scroll);
    m_connections->redraw();
}

void NetworkMonitorDialog::refreshEvents() {
    if (!m_eventList) return;

    const std::vector<SecurityEvent> events =
        SecurityEvents::instance().snapshot();
    if (events.size() < m_shownEvents) {
        // The log was cleared from elsewhere, or wrapped: start again.
        m_eventList->clear();
        m_shownEvents = 0;
    }
    for (std::size_t i = m_shownEvents; i < events.size(); ++i) {
        m_eventList->add(eventLine(events[i]).c_str());
    }
    if (!events.empty()) {
        m_shownEvents = events.size();
        m_eventList->bottomline(m_eventList->size());
    }
}

void NetworkMonitorDialog::onTimer(void* data) {
    NetworkMonitorDialog* dialog = static_cast<NetworkMonitorDialog*>(data);
    if (!dialog) return;
    // Only the visible tab is rebuilt; the other one catches up when selected.
    if (dialog->m_tabs && dialog->m_tabs->value() == dialog->m_eventsTab) {
        dialog->refreshEvents();
    } else {
        dialog->refreshConnections();
    }
    Fl::repeat_timeout(1.0, onTimer, data);
}

void NetworkMonitorDialog::onRefresh(Fl_Widget*, void* data) {
    NetworkMonitorDialog* dialog = static_cast<NetworkMonitorDialog*>(data);
    if (!dialog) return;
    dialog->refreshConnections();
    dialog->refreshEvents();
}

void NetworkMonitorDialog::onClear(Fl_Widget*, void* data) {
    NetworkMonitorDialog* dialog = static_cast<NetworkMonitorDialog*>(data);
    if (!dialog) return;
    SecurityEvents::instance().clear();
    dialog->m_shownEvents = 0;
    if (dialog->m_eventList) dialog->m_eventList->clear();
}

void NetworkMonitorDialog::onClose(Fl_Widget*, void* data) {
    NetworkMonitorDialog* dialog = static_cast<NetworkMonitorDialog*>(data);
    if (dialog) dialog->hide();
}

void NetworkMonitorDialog::show_modal() {
    show();
    while (shown()) Fl::wait();
}
