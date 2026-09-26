// The command-file interpreter: `hydra_uitest --script file.txt` runs one
// "script" test that reads the file line by line. Lets Claude (or anyone)
// poke the real UI without a rebuild. Verb table: docs/agents/ui-testing.md.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "core/strutil.h"
#include "uitest_harness.h"

namespace uitest {

namespace {

using hydra::trim;

// Split "verb rest-of-line" at the first blank.
void split_verb(const std::string& line, std::string& verb, std::string& rest) {
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) {
        verb = line;
        rest.clear();
    } else {
        verb = line.substr(0, sp);
        rest = trim(line.substr(sp + 1));
    }
}

void run_script(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    ctx->SetRef("//Hydra");

    std::ifstream f(h.script_path);
    if (!f) {
        IM_ERRORF("cannot open script %s", h.script_path.c_str());
        return;
    }

    double timeout = 30.0;
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::string verb, rest;
        split_verb(line, verb, rest);
        std::printf("> %s\n", line.c_str());
        std::fflush(stdout);

        if (verb == "window") {
            ctx->SetRef(rest.c_str());
        } else if (verb == "click") {
            ctx->ItemClick(rest.c_str());
        } else if (verb == "check" || verb == "uncheck") {
            if (verb == "check") ctx->ItemCheck(rest.c_str());
            else ctx->ItemUncheck(rest.c_str());
        } else if (verb == "type") {
            // type <ref> | <text>
            size_t bar = rest.find('|');
            if (bar == std::string::npos) {
                IM_ERRORF("line %d: type needs \"<ref> | <text>\"", lineno);
                return;
            }
            std::string ref = trim(rest.substr(0, bar));
            std::string text = trim(rest.substr(bar + 1));
            ctx->ItemInputValue(ref.c_str(), text.c_str());
        } else if (verb == "wait") {
            ctx->SleepNoSkip((float)std::atof(rest.c_str()), 1.0f / 60.0f);
        } else if (verb == "timeout") {
            timeout = std::atof(rest.c_str());
        } else if (verb == "wait-idle") {
            if (!wait_until(ctx, [&] { return !jobs_busy(h); }, timeout)) {
                IM_ERRORF("line %d: jobs still running after %.0fs", lineno, timeout);
                return;
            }
        } else if (verb == "wait-text") {
            if (!wait_until(ctx, [&] { return visible_text(h).find(rest) != std::string::npos; },
                            timeout)) {
                IM_ERRORF("line %d: \"%s\" did not appear within %.0fs", lineno, rest.c_str(),
                          timeout);
                return;
            }
        } else if (verb == "expect-text") {
            ctx->Yield();
            if (visible_text(h).find(rest) == std::string::npos) {
                IM_ERRORF("line %d: \"%s\" is not on screen", lineno, rest.c_str());
                return;
            }
        } else if (verb == "expect-not-text") {
            ctx->Yield();
            if (visible_text(h).find(rest) != std::string::npos) {
                IM_ERRORF("line %d: \"%s\" is on screen", lineno, rest.c_str());
                return;
            }
        } else if (verb == "text") {
            ctx->Yield();
            std::printf("%s\n", visible_text(h).c_str());
        } else if (verb == "dump") {
            ctx->Yield();
            dump_widgets(ctx, rest);
        } else if (verb == "state") {
            dump_state(h);
        } else if (verb == "screenshot") {
            if (!screenshot(ctx, rest)) {
                IM_ERRORF("line %d: screenshot failed", lineno);
                return;
            }
        } else {
            IM_ERRORF("line %d: unknown verb \"%s\"", lineno, verb.c_str());
            return;
        }
        if (ctx->IsError()) {
            std::printf("!! failed at line %d: %s\n", lineno, line.c_str());
            return;
        }
        std::fflush(stdout);
    }
}

}  // namespace

void register_script_test(Harness& h) {
    ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", "script");
    t->UserData = &h;
    t->TestFunc = run_script;
}

}  // namespace uitest
