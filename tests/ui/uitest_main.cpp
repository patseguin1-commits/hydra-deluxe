// hydra_uitest: run Hydra's GUI tests headlessly. See docs/agents/ui-testing.md.
//
//   hydra_uitest --all                 run every checked-in test
//   hydra_uitest --test <name>         run one (e.g. scan, analyze); repeatable
//   hydra_uitest --script <file>       run a command file
//   hydra_uitest --list                list the tests
//   options: --keep-temp  --shots <dir>
//
// Prints [PASS]/[FAIL] per test and exits 0 only if everything passed.

#include <cstdio>
#include <string>
#include <vector>

#include "core/winstr.h"
#include "uitest_harness.h"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: hydra_uitest (--all | --test <name> | --script <file> | --list)"
                 " [--keep-temp] [--shots <dir>]\n");
    return 2;
}

}  // namespace

int main() {
    bool list = false;
    std::vector<std::string> wanted;  // test names, "all", or a script path
    uitest::Harness h;

    const std::vector<std::string> args = hydra::utf8_argv();
    const int argc = static_cast<int>(args.size());
    for (int i = 1; i < argc; ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--all") wanted.push_back("all");
        else if (a == "--list") list = true;
        else if (a == "--keep-temp") h.keep_temp = true;
        else if (a == "--test" || a == "--script") { if (!next(v)) return usage(); wanted.push_back(v); }
        else if (a == "--shots") { if (!next(h.shots_dir)) return usage(); }
        else return usage();
    }
    if (!list && wanted.empty()) return usage();

    if (!h.init()) return 1;
    uitest::register_tests(h);

    if (list) {
        ImVector<ImGuiTest*> tests;
        ImGuiTestEngine_GetTestList(h.engine, &tests);
        for (ImGuiTest* t : tests) std::printf("%s\n", t->Name);
        h.shutdown();
        return 0;
    }

    ImGuiTestEngine_Start(h.engine, ImGui::GetCurrentContext());
    for (const std::string& w : wanted) {
        if (!h.queue(w)) {
            std::fprintf(stderr, "hydra_uitest: no test or script file \"%s\" (try --list)\n",
                         w.c_str());
            h.shutdown();
            return 2;
        }
    }

    // Drive frames until the queue drains; the engine runs the tests between
    // frames on its coroutine thread.
    while (!ImGuiTestEngine_IsTestQueueEmpty(h.engine)) h.frame();
    h.frame();

    int failed = h.print_results(stdout);
    if (h.keep_temp) std::printf("scratch files kept in %s\n", h.temp_dir.c_str());
    std::fflush(stdout);

    h.shutdown();
    return failed == 0 ? 0 : 1;
}
