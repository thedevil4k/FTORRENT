#ifndef NETWORKMONITORDIALOG_H
#define NETWORKMONITORDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include <cstddef>

class TorrentManager;

/**
 * @brief What FTorrent's BitTorrent engine is actually connected to.
 *
 * Two tabs, both fed only by facts the engine reports:
 *
 *   Connections  every peer link right now: torrent, address, port, direction,
 *                encryption, client, rates and progress.
 *   Events       peers that misbehaved and were dropped, invalid requests,
 *                torrent/file errors and tracker failures, newest last.
 *
 * There is no packet capture here and the dialog does not pretend otherwise:
 * it shows the connections FTorrent has, not the traffic on the wire. A pcap
 * view would need a different mechanism (raw sockets or libpcap) and is not
 * part of this build.
 */
class NetworkMonitorDialog : public Fl_Window {
public:
    explicit NetworkMonitorDialog(TorrentManager* manager);
    ~NetworkMonitorDialog() override;

    void show_modal();

private:
    TorrentManager* m_manager;

    Fl_Tabs* m_tabs;
    Fl_Group* m_connectionsTab;
    Fl_Browser* m_connections;
    Fl_Box* m_summary;

    Fl_Group* m_eventsTab;
    Fl_Browser* m_eventList;
    Fl_Box* m_eventSummary;

    Fl_Button* m_refreshButton;
    Fl_Button* m_clearButton;
    Fl_Button* m_closeButton;

    // How many events the list already holds, so a refresh appends instead of
    // rebuilding the whole list every second.
    std::size_t m_shownEvents = 0;

    void createUI();
    void refreshConnections();
    void refreshEvents();
    static void onTimer(void* data);
    static void onRefresh(Fl_Widget* w, void* data);
    static void onClear(Fl_Widget* w, void* data);
    static void onClose(Fl_Widget* w, void* data);
};

#endif // NETWORKMONITORDIALOG_H
