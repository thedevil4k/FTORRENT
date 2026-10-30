// SearchResultsWidget checks.
//
// A search now fills the table from several engines at once, in whatever order
// their batches arrive. What this pins down is the part that would lose rows
// silently: appending batch after batch has to accumulate, never replace, and
// the sorted view has to keep following the sort column as rows land.
#include "SearchResultsWidget.h"
#include "test_support.h"

#include <FL/Fl.H>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using testsupport::ok;
using testsupport::section;

SearchResult result(const std::string& name, int seeders) {
    SearchResult r;
    r.name = name;
    r.seeders = seeders;
    r.leechers = 1;
    r.size = 1024;
    r.source = "test";
    return r;
}

// The widget lays its columns out while it draws, so a headless process has to
// draw it once before the cells mean anything (same rule as the other suites).
void draw(SearchResultsWidget& table) {
    testsupport::forceLayout(&table);
}

} // namespace

int main() {
    section("a first set and then batches");
    {
        SearchResultsWidget table(0, 0, 800, 400);
        table.setResults({result("alpha", 10), result("beta", 5)});
        draw(table);
        ok(table.rowCount() == 2, "setResults paints the first rows");

        // Three engines answering one after another.
        table.appendResults({result("gamma", 20)});
        draw(table);
        ok(table.rowCount() == 3, "a batch is appended, not replacing");

        table.appendResults({result("delta", 1), result("epsilon", 30)});
        draw(table);
        ok(table.rowCount() == 5, "a second batch keeps the previous rows");

        table.appendResults({});
        draw(table);
        ok(table.rowCount() == 5, "an empty batch changes nothing");
    }

    section("sorting follows the rows as they land");
    {
        SearchResultsWidget table(0, 0, 800, 400);
        // The default sort is seeders, descending.
        table.setResults({result("quiet", 1)});
        table.appendResults({result("popular", 50), result("middle", 10)});
        draw(table);

        const SearchResult* top = table.getResultAt(0);
        ok(top != nullptr, "the table has a first row");
        ok(top && top->name == "popular",
           "the most seeded row comes first after appending");
        const SearchResult* bottom = table.getResultAt(2);
        ok(bottom && bottom->name == "quiet",
           "the least seeded row ends up last");
    }

    section("clearing between searches");
    {
        SearchResultsWidget table(0, 0, 800, 400);
        table.setResults({result("one", 1), result("two", 2)});
        table.clear();
        draw(table);
        ok(table.rowCount() == 0, "clear empties the table");

        table.appendResults({result("three", 3)});
        draw(table);
        ok(table.rowCount() == 1, "a new search starts from one row, not two");
    }

    std::printf("\nsearch_results checks=%d failures=%d\n",
                testsupport::checks, testsupport::failures);
    return testsupport::failures ? 1 : 0;
}
