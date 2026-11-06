#ifndef SEARCHSCOPE_H
#define SEARCHSCOPE_H

#include "SearchEngine.h"

#include <vector>

/**
 * @brief Which engines one row of the engine dropdown means.
 *
 * The dropdown has a mode row and one row per enabled engine:
 *
 *   row 0    "All engines" — every enabled engine, each in its own worker.
 *   row n>=1 exactly the nth enabled engine, and nothing else.
 *
 * That second half is a promise to the user: picking a site must produce
 * requests to that site only. It lives here, as a pure function over the
 * catalogue, so a test can hold it down instead of it being a few lines buried
 * in MainWindow::startSearch().
 */
std::vector<SearchEngine::Definition>
searchScopeForRow(const std::vector<SearchEngine::Definition>& catalogue,
                  int row);

/** True for the row that asks every enabled engine at once. */
bool isAllEnginesRow(int row);

#endif // SEARCHSCOPE_H
