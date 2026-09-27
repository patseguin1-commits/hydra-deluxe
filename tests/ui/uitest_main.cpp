// hydra_uitest: run Hydra's GUI tests headlessly. See docs/agents/ui-testing.md.
//
//   hydra_uitest --all                 run every checked-in test
//   hydra_uitest --test <name>         run one (e.g. scan, analyze); repeatable
//   hydra_uitest --script <file>       run a command file
//   hydra_uitest --list                list the tests
//   options: --keep-temp  --shots <dir>  --jobs <n>
//
// --jobs <n> runs each chosen test in its own hydra_uitest process, at most n
// at once, and prints their results in the usual order. Each process has its
// own scratch folder and a fresh ImGui context, so no test sees another's
// leftovers. Without --jobs the tests run one after another in this process.
//
// Prints [PASS]/[FAIL] per test and exits 0 only if everything passed.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/winstr.h"
#include "uitest_harness.h"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: hydra_uitest (--all | --test <name> | --script <file> | --list)"
                 " [--keep-temp] [--shots <dir>] [--jobs <n>]\n");
    return 2;
}

// One test's child process and the file its output goes to.
struct Child {
    std::string name;
    std::string log;
    HANDLE process = nullptr;
    DWORD exit_code = 1;
};

// Starts `hydra_uitest --test <name>` with its stdout and stderr in c.log.
bool launch_child(Child& c, const std::wstring& exe, const std::vector<std::string>& passthrough) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};  // the child inherits the file
    HANDLE out = CreateFileW(hydra::utf8_to_wide(c.log).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return false;
    std::wstring cmd = L"\"" + exe + L"\" --test \"" + hydra::utf8_to_wide(c.name) + L"\"";
    for (const std::string& p : passthrough) cmd += L" \"" + hydra::utf8_to_wide(p) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    CloseHandle(out);  // the child holds its own copy
    if (!ok) return false;
    CloseHandle(pi.hThread);
    c.process = pi.hProcess;
    return true;
}

// --jobs: every chosen test in its own process, at most `jobs` at once.
int run_parallel(uitest::Harness& h, const std::vector<std::string>& wanted, int jobs,
                 const std::vector<std::string>& passthrough) {
    // The chosen tests in registration order, the order --all runs them in.
    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(h.engine, &tests);
    std::vector<Child> children;
    for (ImGuiTest* t : tests) {
        if (std::strcmp(t->Name, "script") == 0) continue;
        for (const std::string& w : wanted) {
            if (w == "all" || w == t->Name) {
                Child c;
                c.name = t->Name;
                c.log = h.temp_dir + "\\" + c.name + ".log";
                children.push_back(c);
                break;
            }
        }
    }
    for (const std::string& w : wanted) {
        bool found = w == "all";
        for (const Child& c : children) found = found || c.name == w;
        if (!found) {
            std::fprintf(stderr,
                         "hydra_uitest: no test \"%s\" (--jobs runs named tests, not scripts)\n",
                         w.c_str());
            return 2;
        }
    }

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    std::vector<size_t> running;
    size_t next = 0, done = 0;
    while (done < children.size()) {
        while (running.size() < static_cast<size_t>(jobs) && next < children.size()) {
            Child& c = children[next];
            if (launch_child(c, exe, passthrough)) {
                running.push_back(next);
            } else {
                std::fprintf(stderr, "hydra_uitest: could not start the process for %s\n",
                             c.name.c_str());
                ++done;
            }
            ++next;
        }
        if (running.empty()) continue;
        std::vector<HANDLE> handles;
        for (size_t i : running) handles.push_back(children[i].process);
        const DWORD r = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(),
                                               FALSE, INFINITE);
        if (r >= WAIT_OBJECT_0 + handles.size()) {
            std::fprintf(stderr, "hydra_uitest: waiting on the test processes failed\n");
            return 1;
        }
        const size_t k = r - WAIT_OBJECT_0;
        Child& c = children[running[k]];
        GetExitCodeProcess(c.process, &c.exit_code);
        CloseHandle(c.process);
        c.process = nullptr;
        running.erase(running.begin() + static_cast<std::ptrdiff_t>(k));
        ++done;
        std::fprintf(stderr, "[%zu/%zu] %s %s\n", done, children.size(),
                     c.exit_code == 0 ? "done" : "FAILED", c.name.c_str());
    }

    // Each child printed its own [PASS]/[FAIL] line and, on a failure, its log.
    int failed = 0;
    for (const Child& c : children) {
        std::string text;
        try {
            text = hydra::read_file_text(c.log);
        } catch (const std::runtime_error&) {
        }
        std::fputs(text.c_str(), stdout);
        if (c.exit_code != 0) {
            ++failed;
            // A crash prints no result line of its own.
            if (text.find("[FAIL]") == std::string::npos)
                std::printf("[FAIL] hydra/%s\n---- log ----\nthe test process ended with exit "
                            "code 0x%08lX and printed no result\n---- end ----\n",
                            c.name.c_str(), static_cast<unsigned long>(c.exit_code));
        }
    }
    std::printf("%zu of %zu passed\n", children.size() - static_cast<size_t>(failed),
                children.size());
    std::fflush(stdout);
    return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
    bool list = false;
    int jobs = 1;
    std::vector<std::string> wanted;       // test names, "all", or a script path
    std::vector<std::string> passthrough;  // options each --jobs child gets too
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
        else if (a == "--keep-temp") { h.keep_temp = true; passthrough.push_back(a); }
        else if (a == "--test" || a == "--script") { if (!next(v)) return usage(); wanted.push_back(v); }
        else if (a == "--shots") {
            if (!next(h.shots_dir)) return usage();
            passthrough.push_back(a);
            passthrough.push_back(h.shots_dir);
        } else if (a == "--jobs") {
            if (!next(v)) return usage();
            jobs = std::atoi(v.c_str());
            if (jobs < 1) return usage();
        } else return usage();
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

    if (jobs > 1) {
        const int rc = run_parallel(h, wanted, jobs, passthrough);
        h.shutdown();
        return rc;
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
