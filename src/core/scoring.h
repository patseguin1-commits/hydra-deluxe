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

namespace hydra {

struct CategoryScores {
    int base = 0;
    int combo = 0;
    int sp = 0;
    int accent = 0;
    int ghost = 0;
    int sqout_reduction = 0;
};

// Notes are read base-sorted (Chord::notes(true)); the tie order is
// observable through the SqOut calculation, which reads note 0.
//
// If per_note is given, it is filled with one CategoryScores per note, in
// that same order, holding each note's own share of the six totals below.
// This is only for display/tooling use — it does not change the aggregate
// that gets returned.
CategoryScores category_scores(const Chord& chord, int combo,
                                std::vector<CategoryScores>* per_note = nullptr);

}  // namespace hydra

#endif  // HYDRA_CORE_SCORING_H
