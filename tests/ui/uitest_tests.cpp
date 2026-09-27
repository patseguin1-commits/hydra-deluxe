// The checked-in GUI tests. Each one starts from a fresh scratch app
// (reset_app), drives the real UI by widget label, and checks both the app's
// state and what is on screen. See docs/agents/ui-testing.md for the label
// cheat-sheet and how to add one.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "uitest_harness.h"

namespace uitest {

void register_paths_tests(Harness& h);  // uitest_paths.cpp

void register_tests(Harness& h) {
    // --all runs the tests in the order they are registered, and the ImGui
    // context carries over from one test to the next. So the 26 tests that
    // lived in this one file keep their old order. A test not named here (a
    // later task's new test) runs after them, in its file's order.
    static const char* const kRunOrder[] = {
        "scan", "analyze", "cap-switch", "preview", "difficulty",
        "analyze-on-preview", "preview-path-overlay", "preview-controls",
        "preview-drain-box", "preview-overlay-fit", "preview-buttons-keys",
        "scrub-hold", "layout-drift", "batch-modal-drift", "settings-and-reports",
        "dynamics", "dynamics-stored", "stars", "rules-error",
        "squeezed_out_uncounted", "details-close-teardown", "library-state-per-app",
        "dm-compare-flow", "report-buttons", "view-settings",
    };
    std::vector<TestEntry> pending;
    for (const std::vector<TestEntry>* area :
         {&library_tests(), &details_tests(), &preview_tests(), &batch_report_tests()})
        pending.insert(pending.end(), area->begin(), area->end());

    std::vector<TestEntry> ordered;
    for (const char* name : kRunOrder) {
        auto it = std::find_if(pending.begin(), pending.end(), [&](const TestEntry& e) {
            return std::strcmp(e.name, name) == 0;
        });
        if (it == pending.end()) {
            // A renamed or deleted test must be renamed here too, or --all
            // would silently skip it.
            std::fprintf(stderr, "hydra_uitest: no test file registers \"%s\"\n", name);
            std::abort();
        }
        ordered.push_back(*it);
        pending.erase(it);
    }
    ordered.insert(ordered.end(), pending.begin(), pending.end());

    for (const TestEntry& e : ordered) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
    register_paths_tests(h);
}

}  // namespace uitest
