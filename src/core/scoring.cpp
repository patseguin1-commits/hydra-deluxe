#include "core/scoring.h"

#include <vector>

#include "core/timing.h"  // to_multiplier

namespace hydra {

CategoryScores category_scores(const Chord& chord, int combo,
                                std::vector<CategoryScores>* per_note,
                                core::SqOutRule sqout_rule) {
    // Every possible cross-multiplication of the score multipliers, named as
    // in the original.
    int base_note = 0, base_cymbal = 0;
    int combo_note = 0, combo_cymbal = 0;
    int sp_note = 0, sp_cymbal = 0;
    int combosp_note = 0, combosp_cymbal = 0;
    int dynamic_note_accent = 0, dynamic_cymbal = 0;
    int dynamic_note_ghost = 0;
    int combodynamic_note = 0, combodynamic_cymbal = 0;
    int spdynamic_note = 0, spdynamic_cymbal = 0;
    int combospdynamic_note = 0, combospdynamic_cymbal = 0;

    int sqout_reduction = 0;
    int first_multiplier = to_multiplier(combo);
    int last_multiplier = first_multiplier;

    const std::vector<ChordNote> ordering = chord.notes(true);
    if (per_note) {
        per_note->clear();
        per_note->reserve(ordering.size());
    }
    for (size_t i = 0; i < ordering.size(); ++i) {
        const ChordNote& note = ordering[i];
        const bool is_cymbal = note.is_cymbal();
        const bool is_accent = note.is_accent();
        const bool is_ghost = note.is_ghost();
        // is_dynamic() is dynamictype != NORMAL, i.e. accent or ghost.
        const bool is_dynamic = is_accent || is_ghost;

        combo += 1;
        const int combo_multiplier = to_multiplier(combo);
        const int extra = combo_multiplier - 1;
        if (i == 0) first_multiplier = combo_multiplier;
        last_multiplier = combo_multiplier;

        const int basevalue = kNoteBasePoints;
        const int cymbvalue = kCymbalBonusPoints;

        const int cymb = is_cymbal ? cymbvalue : 0;
        const int dyn_cymb = (is_cymbal && is_dynamic) ? cymbvalue : 0;

        base_note += basevalue;
        base_cymbal += cymb;
        combo_note += basevalue * extra;
        combo_cymbal += cymb * extra;
        sp_note += basevalue;
        sp_cymbal += cymb;
        combosp_note += basevalue * extra;
        combosp_cymbal += cymb * extra;
        dynamic_note_accent += is_accent ? basevalue : 0;
        dynamic_note_ghost += is_ghost ? basevalue : 0;
        dynamic_cymbal += dyn_cymb;
        combodynamic_note += is_dynamic ? basevalue * extra : 0;
        combodynamic_cymbal += dyn_cymb * extra;
        spdynamic_note += is_dynamic ? basevalue : 0;
        spdynamic_cymbal += dyn_cymb;
        combospdynamic_note += is_dynamic ? basevalue * extra : 0;
        combospdynamic_cymbal += dyn_cymb * extra;

        // SqOut: the notes that lose their SP doubling. FirstNote takes note 0
        // only; WholeChord takes every note. A lost doubling is the note's full
        // value at its own multiplier, and basescore() is that value.
        const bool loses_sp = i == 0 || sqout_rule == core::SqOutRule::WholeChord;
        const int note_sqout = loses_sp ? note.basescore() * combo_multiplier : 0;
        sqout_reduction += note_sqout;

        if (per_note) {
            CategoryScores note_scores;
            note_scores.base = basevalue + cymb + dyn_cymb;
            note_scores.combo = basevalue * extra + cymb * extra +
                                 (is_dynamic ? basevalue * extra : 0) +
                                 dyn_cymb * extra;
            note_scores.sp = basevalue + cymb + basevalue * extra +
                              cymb * extra + (is_dynamic ? basevalue : 0) +
                              dyn_cymb + (is_dynamic ? basevalue * extra : 0) +
                              dyn_cymb * extra;
            note_scores.accent = is_accent ? basevalue : 0;
            note_scores.ghost = is_ghost ? basevalue : 0;
            note_scores.multiplier = combo_multiplier;
            note_scores.multiplier_after = combo_multiplier;
            // The dynamics terms the totals above add for this note:
            // dynamic_note_* and combodynamic_note (the pad's basevalue) plus
            // dynamic_cymbal and combodynamic_cymbal (dyn_cymb), each paid
            // once at 1x and once more per extra.
            note_scores.dynamics_bonus =
                ((is_dynamic ? basevalue : 0) + dyn_cymb) * combo_multiplier;
            note_scores.sqout_reduction = note_sqout;
            per_note->push_back(note_scores);
        }
    }

    CategoryScores out;
    out.base = base_note + base_cymbal + dynamic_cymbal;
    out.combo =
        combo_note + combo_cymbal + combodynamic_note + combodynamic_cymbal;
    out.sp = sp_note + sp_cymbal + combosp_note + combosp_cymbal +
             spdynamic_note + spdynamic_cymbal + combospdynamic_note +
             combospdynamic_cymbal;
    out.accent = dynamic_note_accent;
    out.ghost = dynamic_note_ghost;
    out.sqout_reduction = sqout_reduction;
    out.multiplier = first_multiplier;
    out.multiplier_after = last_multiplier;
    return out;
}

}  // namespace hydra
