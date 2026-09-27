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

namespace {

// Every field that can change a run's answer, one line each. The text is
// frozen: changing it makes every stored result Stale (docs/adr/0014).
std::string fixed_cap_text(const Rules& r) {
    std::string text;
    add_line(text, "backend_leeway_ms", r.backend_leeway_ms);
    text += r.sqout_rule == SqOutRule::WholeChord ? "sqout_rule=whole_chord\n"
                                                  : "sqout_rule=first_note\n";
    add_line(text, "max_tied_paths", r.max_tied_paths);
    add_line(text, "fill_cooldown_measures", r.fill_cooldown_measures);
    add_line(text, "fill_max_distance_beats", r.fill_max_distance_beats);
    add_line(text, "fill_length_measures", r.fill_length_measures);
    add_line(text, "fill_land_slop_beats", r.fill_land_slop_beats);
    return text;
}

// 0 is reserved for "no usable rules"; a hash that lands on it moves off.
uint64_t hash_rules_text(const std::string& text) {
    const uint64_t h = fnv1a64(text);
    return h == kNoRulesFingerprint ? 1 : h;
}

}  // namespace

uint64_t Rules::fingerprint() const { return hash_rules_text(fixed_cap_text(*this)); }

uint64_t Rules::retired_auto_fingerprint() const {
    // Byte for byte what 1.8.4's auto_fingerprint() hashed under the default
    // ladder. Pinned by the test "rules: the retired Auto fingerprint is what
    // Hydra 1.8.4 stamped".
    return hash_rules_text(fixed_cap_text(*this) +
                           "auto_cap_ladder=16,32,64,128,256,512,\n");
}

const Rules& default_rules() {
    static const Rules rules;
    return rules;
}

const RulesStamp& default_stamp() {
    static const RulesStamp stamp = RulesStamp::of(default_rules());
    return stamp;
}

}  // namespace hydra::core
