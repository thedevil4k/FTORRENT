#include "SecurityEvents.h"

#include <chrono>

SecurityEvents& SecurityEvents::instance() {
    static SecurityEvents events;
    return events;
}

void SecurityEvents::add(const std::string& torrent, const std::string& kind,
                         const std::string& detail) {
    SecurityEvent event;
    event.when = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    event.torrent = torrent;
    event.kind = kind;
    event.detail = detail;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(std::move(event));
    while (m_events.size() > kCapacity) m_events.pop_front();
}

std::vector<SecurityEvent> SecurityEvents::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<SecurityEvent>(m_events.begin(), m_events.end());
}

std::size_t SecurityEvents::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events.size();
}

void SecurityEvents::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
}
