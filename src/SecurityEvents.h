#ifndef SECURITYEVENTS_H
#define SECURITYEVENTS_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief One thing worth noticing that the BitTorrent engine reported.
 *
 * These are the events the network monitor shows: a peer that misbehaved and
 * was dropped, an invalid request, a torrent or file error, a tracker that
 * failed. They come from libtorrent's alert queue, which is the only honest
 * source of connection-level facts: FTorrent sees the peers it talks to, not
 * the packets on the wire.
 */
struct SecurityEvent {
    std::int64_t when = 0; // Seconds since the epoch
    std::string torrent;   // Torrent name, may be empty
    std::string kind;      // "Peer banned", "Invalid request", ...
    std::string detail;    // libtorrent's own sentence, shown as-is
};

/**
 * @brief A bounded, thread-safe log of those events.
 *
 * The engine thread appends; the dialog takes a snapshot. The cap is there so
 * a long session with a hostile swarm cannot grow the log without bound: the
 * oldest event is dropped, not the newest, because the recent past is what a
 * monitor is for.
 */
class SecurityEvents {
public:
    static SecurityEvents& instance();

    static const std::size_t kCapacity = 300;

    void add(const std::string& torrent, const std::string& kind,
             const std::string& detail);

    // Oldest first.
    std::vector<SecurityEvent> snapshot() const;

    std::size_t size() const;

    void clear();

private:
    SecurityEvents() = default;

    mutable std::mutex m_mutex;
    std::deque<SecurityEvent> m_events;
};

#endif // SECURITYEVENTS_H
