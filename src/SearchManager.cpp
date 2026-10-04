#include "SearchManager.h"

SearchManager& SearchManager::instance() {
    static SearchManager instance;
    return instance;
}

void SearchManager::addEngine(const SearchEngine::Definition& def) {
    if (def.name.empty() || def.urlTemplate.empty()) return;
    
    // Replace an engine with the same name instead of duplicating it
    for (auto& existing : m_engines) {
        if (existing.name == def.name) {
            existing = def;
            return;
        }
    }
    m_engines.push_back(def);
}
