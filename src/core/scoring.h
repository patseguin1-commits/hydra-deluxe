// Per-chord score breakdown — the hottest function in chart analysis.
//
// The loop is a line-for-line transcription of the original category_scores,
// including the duplicated sp_* accumulators, which mirror base_*/combo_*
// exactly. They are kept rather than folded together so a change to the
// scoring rules can be diffed against the history; the compiler collapses
// them anyway.

#ifndef HYDRA_CORE_SCORING_H
#define HYDRA_CORE_SCORING_H

#include <vector>

#include "core/model.h"
#include "core/rules.h"

namespace hydra {

struct CategoryScores {
    int base = 0;
    int combo = 0;
    int sp = 0;
    int accent = 0;
    int ghost = 0;
    int sqout_reduction = 0;
    // The combo multiplier applied to the chord's first note (base-sorted),
    // the same note the SqOut calculation reads. Per note, that note's own.
    int multiplier = 1;
    // The multiplier applied to the chord's last note: what the game's disc
    // shows once the whole chord is hit. Per note, the same as `multiplier`.
    int multiplier_after = 1;
    // Per note only (left 0 on the chord total): the points this note's
    // ghost or accent earns, multiplier included -- the pad's 50 plus the
    // cymbal's 15 when the note is a dynamic cymbal. A mis-hit dynamic note
    // loses exactly this, twice over inside Star Power. `accent` and `ghost`
    // above hold only the pad's 50, before the multiplier.
    int dynamics_bonus = 0;
};

// Notes are read base-sorted (Chord::notes(true)); the tie order is
// observable through the SqOut calculation, which reads note 0.
//
// If per_note is given, it is filled with one CategoryScores per note, in
// that same order, holding each note's own share of the six totals below.
// This is only for display/tooling use — it does not change the aggregate
// that gets returned.
// sqout_rule decides which notes' SP doubling sqout_reduction takes: only
// note 0 (FirstNote, Hydra's rule so far) or every note (WholeChord).
CategoryScores category_scores(const Chord& chord, int combo,
                                std::vector<CategoryScores>* per_note = nullptr,
                                core::SqOutRule sqout_rule = core::SqOutRule::FirstNote);

}  // namespace hydra

#endif  // HYDRA_CORE_SCORING_H
