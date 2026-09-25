#include "core/rules.h"

#include <cstdio>
#include <string>

namespace hydra::core {

namespace {

void add_line(std::string& out, const char* key, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s=%.17g\n", key, v);
    out += buf;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace

uint64_t Rules::fingerprint() const {
    std::string text;
    add_line(text, "backend_leeway_ms", backend_leeway_ms);
    text += sqout_rule == SqOutRule::WholeChord ? "sqout_rule=whole_chord\n"
                                                : "sqout_rule=first_note\n";
    add_line(text, "max_tied_paths", max_tied_paths);
    text += "auto_cap_ladder=";
    for (int cap : auto_cap_ladder) text += std::to_string(cap) + ",";
    text += "\n";
    add_line(text, "auto_budget_s", auto_budget_s);
    add_line(text, "fill_cooldown_measures", fill_cooldown_measures);
    add_line(text, "fill_max_distance_beats", fill_max_distance_beats);
    add_line(text, "fill_length_measures", fill_length_measures);
    add_line(text, "fill_land_slop_beats", fill_land_slop_beats);
    const uint64_t h = fnv1a64(text);
    // 0 is reserved for "no usable rules"; a hash that lands on it moves off.
    return h == kNoRulesFingerprint ? 1 : h;
}

const Rules& default_rules() {
    static const Rules rules;
    return rules;
}

}  // namespace hydra::core
