#include "SearchScope.h"

bool isAllEnginesRow(int row) {
    return row == 0;
}

std::vector<SearchEngine::Definition> searchScopeForRow(
    const std::vector<SearchEngine::Definition>& catalogue, int row) {
    std::vector<SearchEngine::Definition> scope;
    if (row < 0) return scope;

    int current = 1;   // enabled engines are numbered from 1, after the mode row
    for (const SearchEngine::Definition& def : catalogue) {
        if (!def.enabled) continue;
        if (row == 0) {
            scope.push_back(def);       // all-engines mode: every enabled site
        } else if (current == row) {
            scope.push_back(def);       // one site, and nothing else
            return scope;
        }
        ++current;
    }
    return scope;
}
