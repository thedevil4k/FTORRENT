// SearchScope checks.
//
// The promise being tested is the one the user cares about: pick a site and the
// app talks to that site only. The all-engines mode is explicit, never implied.
#include "SearchScope.h"

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

SearchEngine::Definition engine(const std::string& name, bool enabled = true) {
    SearchEngine::Definition def;
    def.name = name;
    def.enabled = enabled;
    return def;
}

} // namespace

int main() {
    // Three enabled sites and one disabled, in catalogue order.
    const std::vector<SearchEngine::Definition> catalogue = {
        engine("First"),
        engine("Disabled One", false),
        engine("Second"),
        engine("Third"),
    };

    section("one row, one engine");
    {
        const auto row1 = searchScopeForRow(catalogue, 1);
        ok(row1.size() == 1, "row 1 asks exactly one engine");
        ok(!row1.empty() && row1[0].name == "First",
           "row 1 is the first enabled engine");

        const auto row2 = searchScopeForRow(catalogue, 2);
        ok(row2.size() == 1, "row 2 asks exactly one engine");
        ok(!row2.empty() && row2[0].name == "Second",
           "row 2 skips the disabled engine");

        const auto row3 = searchScopeForRow(catalogue, 3);
        ok(row3.size() == 1 && row3[0].name == "Third",
           "row 3 is the third enabled engine");

        // A wider search must never happen by accident.
        for (int row = 1; row <= 3; ++row) {
            ok(searchScopeForRow(catalogue, row).size() == 1,
               "no single-engine row ever asks more than one engine");
        }
    }

    section("the all-engines mode is explicit");
    {
        const auto all = searchScopeForRow(catalogue, 0);
        ok(all.size() == 3, "row 0 asks every enabled engine");
        ok(isAllEnginesRow(0), "row 0 is the mode row");
        ok(!isAllEnginesRow(1), "row 1 is not the mode row");

        bool sawDisabled = false;
        for (const auto& def : all) {
            if (def.name == "Disabled One") sawDisabled = true;
        }
        ok(!sawDisabled, "a disabled engine is never asked, not even by the mode");
    }

    section("rows that mean nothing");
    {
        ok(searchScopeForRow(catalogue, 4).empty(),
           "a row past the last engine asks nothing");
        ok(searchScopeForRow(catalogue, -1).empty(),
           "a negative row asks nothing");
        ok(searchScopeForRow({}, 0).empty(),
           "an empty catalogue asks nothing");
        ok(searchScopeForRow({engine("Off", false)}, 0).empty(),
           "a catalogue with only disabled engines asks nothing");
    }

    std::cout << "\nsearch_scope checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
