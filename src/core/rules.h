// The user's own rule choices, in one place. Every value here is a judgment
// call, not a fact about Clone Hero, so it is configurable through
// hydra_rules.ini (app/rules_file.h). The defaults are the values Hydra always
// used, so an absent file changes nothing. A stored record carries
// fingerprint() of the rules it ran under (docs/adr/0014).
// In hydra_rules.ini each field is set by its own name, e.g. `max_tied_paths = 4`.

#ifndef HYDRA_CORE_RULES_H
#define HYDRA_CORE_RULES_H

#include <cstdint>
#include <vector>

namespace hydra::core {

// Which notes of a squeezed-out chord lose their SP doubling.
//   FirstNote  -- only the first note in base-sorted order (Hydra's rule so far).
//   WholeChord -- every note in the chord.
enum class SqOutRule { FirstNote, WholeChord };

// The fingerprint of "no usable rules". Rules::fingerprint() never returns
// it, so a store gated on it (a bad hydra_rules.ini) reads no row as Ready.
constexpr uint64_t kNoRulesFingerprint = 0;

struct Rules {
    // A backend note this close after the SP end still scores under SP.
    double backend_leeway_ms = 3.0;
    SqOutRule sqout_rule = SqOutRule::FirstNote;
    // Tied paths the engine folds into one leader before it drops the rest.
    int max_tied_paths = 4;
    // Auto cap: the SP ceilings tried in order, and the seconds before a slow
    // rung is abandoned.
    std::vector<int> auto_cap_ladder{16, 32, 64, 128, 256, 512};
    double auto_budget_s = 120.0;
    // Generated fills (Song::check_activations): the fewest measures between
    // two fills, how far from a downbeat the chosen chord may sit, and the
    // fill's length.
    int fill_cooldown_measures = 4;
    double fill_max_distance_beats = 0.5;
    double fill_length_measures = 0.5;
    // Authored-fill placement (fill_lands_on_chord, both parsers): how close
    // the next chord must be to the fill end to count as the chord the fill
    // lands on.
    double fill_land_slop_beats = 1.0 / 32;

    // A 64-bit hash of every field above. Equal rules give equal fingerprints
    // in every build; any changed field gives a different one. Never
    // kNoRulesFingerprint.
    uint64_t fingerprint() const;
};

// The defaults above, as one shared value.
const Rules& default_rules();

}  // namespace hydra::core

#endif  // HYDRA_CORE_RULES_H
