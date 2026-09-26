#include "app/rules_file.h"

#include <fstream>
#include <sstream>
#include <string>

#include "app/config.h"
#include "core/strutil.h"

namespace hydra::app {

namespace {

[[noreturn]] void bad(const std::string& path, const std::string& key,
                      const std::string& value, const char* want) {
    throw RulesFileError(path + ": " + key + " = \"" + value + "\" is not " + want);
}

double to_double(const std::string& path, const std::string& key, const std::string& v,
                 double min_inclusive) {
    size_t used = 0;
    double d = 0;
    try { d = std::stod(v, &used); } catch (...) { bad(path, key, v, "a number"); }
    if (used != v.size()) bad(path, key, v, "a number");
    if (d < min_inclusive) bad(path, key, v, "in range");
    return d;
}

int to_int(const std::string& path, const std::string& key, const std::string& v,
           int min_inclusive) {
    size_t used = 0;
    int i = 0;
    try { i = std::stoi(v, &used); } catch (...) { bad(path, key, v, "a whole number"); }
    if (used != v.size()) bad(path, key, v, "a whole number");
    if (i < min_inclusive) bad(path, key, v, "in range");
    return i;
}

}  // namespace

core::Rules load_rules_file(const std::filesystem::path& path) {
    core::Rules r;
    std::ifstream f(path);
    if (!f) return r;  // no file: today's rules
    const std::string where = path.u8string();

    std::string line;
    while (std::getline(f, line)) {
        // Everything from a # on is a comment, whole-line or trailing.
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            throw RulesFileError(where + ": \"" + line + "\" is not key = value");
        const std::string key = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if (key == "backend_leeway_ms") r.backend_leeway_ms = to_double(where, key, v, 0.0);
        else if (key == "sqout_rule") {
            if (v == "first_note") r.sqout_rule = core::SqOutRule::FirstNote;
            else if (v == "whole_chord") r.sqout_rule = core::SqOutRule::WholeChord;
            else bad(where, key, v, "first_note or whole_chord");
        }
        else if (key == "max_tied_paths") r.max_tied_paths = to_int(where, key, v, 1);
        else if (key == "auto_cap_ladder") {
            r.auto_cap_ladder.clear();
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                int cap = to_int(where, key, trim(item), 1);
                if (!r.auto_cap_ladder.empty() && cap <= r.auto_cap_ladder.back())
                    bad(where, key, v, "a rising list of caps");
                r.auto_cap_ladder.push_back(cap);
            }
            if (r.auto_cap_ladder.empty()) bad(where, key, v, "a rising list of caps");
        }
        else if (key == "auto_budget_s") {
            const double budget = to_double(where, key, v, 0.0);
            if (budget == 0.0) bad(where, key, v, "above zero");
            r.auto_budget_s = budget;
        }
        else if (key == "fill_cooldown_measures") r.fill_cooldown_measures = to_int(where, key, v, 1);
        else if (key == "fill_max_distance_beats") r.fill_max_distance_beats = to_double(where, key, v, 0.0);
        else if (key == "fill_length_measures") {
            r.fill_length_measures = to_double(where, key, v, 0.0);
            if (r.fill_length_measures == 0.0) bad(where, key, v, "above zero");
        }
        else if (key == "fill_land_slop_beats") r.fill_land_slop_beats = to_double(where, key, v, 0.0);
        else throw RulesFileError(where + ": unknown key \"" + key + "\"");
    }
    return r;
}

std::filesystem::path default_rules_path() {
    if (!path_overrides().rules_path.empty())
        return std::filesystem::u8path(path_overrides().rules_path);
    return std::filesystem::u8path(exe_dir()) / "hydra_rules.ini";
}

}  // namespace hydra::app
