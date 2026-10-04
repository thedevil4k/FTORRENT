#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include "SearchEngine.h"
#include <vector>

/**
 * @brief Holds the predefined search engines for the session.
 *
 * There is no plugin system anymore: the catalogue lives in
 * SearchEngine::builtinEngines() and is loaded fresh on every start.
 */
class SearchManager {
public:
    static SearchManager& instance();

    const std::vector<SearchEngine::Definition>& engines() const { return m_engines; }

    void addEngine(const SearchEngine::Definition& def);

    // Drops every engine (used when the built-in catalogue is refreshed)
    void clear() { m_engines.clear(); }

private:
    SearchManager() = default;

    std::vector<SearchEngine::Definition> m_engines;
};

#endif // SEARCHMANAGER_H
