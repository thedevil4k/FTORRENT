// SecurityEvents checks.
//
// The log is a ring buffer shared by two threads: the engine appends while the
// monitor reads a snapshot. These checks pin down the order, the cap and that
// old events are the ones dropped -- a monitor that forgets the newest event is
// worse than one that forgets the oldest.
#include "SecurityEvents.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const std::string& what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

} // namespace

int main() {
    SecurityEvents& events = SecurityEvents::instance();
    events.clear();

    section("empty log");
    ok(events.size() == 0, "a cleared log has no events");
    ok(events.snapshot().empty(), "a cleared log snapshots to nothing");

    section("order and fields");
    {
        events.add("ubuntu.iso", "Peer banned", "peer 10.0.0.1 misbehaved");
        events.add("", "Tracker error", "http://t.example: connection refused");
        events.add("debian.iso", "Invalid request", "request for a piece we do not have");

        const std::vector<SecurityEvent> all = events.snapshot();
        ok(all.size() == 3, "three events are kept");
        ok(all[0].kind == "Peer banned", "the oldest event comes first");
        ok(all[2].kind == "Invalid request", "the newest event comes last");
        ok(all[0].torrent == "ubuntu.iso", "the torrent name is kept");
        ok(all[1].torrent.empty(), "an event with no torrent is allowed");
        ok(all[0].when > 0, "an event carries a timestamp");
        ok(!all[0].detail.empty(), "an event carries the engine's sentence");
    }

    section("the cap keeps the newest");
    {
        events.clear();
        const std::size_t overflow = SecurityEvents::kCapacity + 7;
        for (std::size_t i = 0; i < overflow; ++i) {
            events.add("torrent", "Peer error",
                       "event " + std::to_string(i));
        }
        ok(events.size() == SecurityEvents::kCapacity,
           "the log stops at its capacity");
        const std::vector<SecurityEvent> all = events.snapshot();
        ok(all.size() == SecurityEvents::kCapacity,
           "the snapshot has the same size");
        ok(all.back().detail == "event " + std::to_string(overflow - 1),
           "the newest event survives");
        ok(all.front().detail == "event 7",
           "the oldest events are the ones dropped");
    }

    section("clearing");
    {
        events.clear();
        ok(events.size() == 0, "clearing empties the log");
        events.add("torrent", "Peer banned", "after clear");
        ok(events.size() == 1, "the log still accepts events after a clear");
        ok(events.snapshot().front().detail == "after clear",
           "the first event after a clear is the new one");
    }

    std::cout << "\nsecurity_events checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
