// Native scoring core for Hydra.
//
// Ports hypath.category_scores -- the hottest function in chart analysis --
// to C++ behind a plain C ABI. The C ABI (rather than a CPython extension)
// is deliberate: it builds with either MSVC or a no-admin portable MinGW,
// and it does not have to match CPython's ABI, so a Python upgrade cannot
// silently break the binary.
//
// Parity with the Python original is the whole point of this file. The loop
// below is a line-for-line transcription of category_scores, including the
// duplicated sp_* accumulators, which mirror base_*/combo_* exactly. They
// are kept rather than folded together so that a future change to the Python
// can be diffed against this one; the compiler collapses them anyway.
//
// Notes must arrive already base-sorted, matching
// chord.notes(basesorted=True). Sorting stays in Python because Python's
// sort is stable and the tie order is observable through the SqOut
// calculation, which reads note 0.

#include <cstdint>

#include "hydra_score.h"

namespace {

// hymisc.to_multiplier
inline int32_t to_multiplier(int32_t combo) {
    if (combo < 10) return 1;
    if (combo < 20) return 2;
    if (combo < 30) return 3;
    return 4;
}

}  // namespace

extern "C" {

int32_t hy_category_scores(const uint8_t* note_flags,
                           int32_t note_count,
                           int32_t combo,
                           int32_t flag_skipped_dynamics,
                           hy_scores* out) {
    if (out == nullptr) return HY_ERR_NULL_OUT;
    if (note_flags == nullptr && note_count != 0) return HY_ERR_NULL_NOTES;
    if (note_count < 0) return HY_ERR_BAD_COUNT;

    // Every possible cross-multiplication of the score multipliers, named as
    // in the Python.
    int32_t base_note = 0, base_cymbal = 0;
    int32_t combo_note = 0, combo_cymbal = 0;
    int32_t sp_note = 0, sp_cymbal = 0;
    int32_t combosp_note = 0, combosp_cymbal = 0;
    int32_t dynamic_note_accent = 0, dynamic_cymbal = 0;
    int32_t dynamic_note_ghost = 0;
    int32_t combodynamic_note = 0, combodynamic_cymbal = 0;
    int32_t spdynamic_note = 0, spdynamic_cymbal = 0;
    int32_t combospdynamic_note = 0, combospdynamic_cymbal = 0;

    int32_t sqout_reduction = 0;
    int32_t skipped_dynamic_reduction = 0;

    for (int32_t i = 0; i < note_count; ++i) {
        const uint8_t f = note_flags[i];
        const bool is_cymbal = (f & HY_NOTE_CYMBAL) != 0;
        const bool is_accent = (f & HY_NOTE_ACCENT) != 0;
        const bool is_ghost = (f & HY_NOTE_GHOST) != 0;
        const bool is_activation = (f & HY_NOTE_ACTIVATION) != 0;
        // hydata: is_dynamic() is dynamictype != NORMAL, i.e. accent or ghost.
        const bool is_dynamic = is_accent || is_ghost;

        combo += 1;
        const int32_t combo_multiplier = to_multiplier(combo);
        const int32_t extra = combo_multiplier - 1;

        const int32_t basevalue = 50;
        const int32_t cymbvalue = 15;

        const int32_t cymb = is_cymbal ? cymbvalue : 0;
        const int32_t dyn_cymb = (is_cymbal && is_dynamic) ? cymbvalue : 0;

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

        // Quick and dirty SqOut calculation -- first note only.
        if (i == 0) {
            sqout_reduction =
                (basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1);
        }

        if (flag_skipped_dynamics && is_activation && is_dynamic) {
            skipped_dynamic_reduction =
                basevalue + cymb + basevalue * extra + cymb * extra;
        }
    }

    out->base = base_note + base_cymbal + dynamic_cymbal;
    out->combo = combo_note + combo_cymbal + combodynamic_note + combodynamic_cymbal;
    out->sp = sp_note + sp_cymbal + combosp_note + combosp_cymbal
            + spdynamic_note + spdynamic_cymbal
            + combospdynamic_note + combospdynamic_cymbal;
    out->accent = dynamic_note_accent;
    out->ghost = dynamic_note_ghost;
    out->sqout_reduction = sqout_reduction;
    out->skipped_dynamic_reduction = skipped_dynamic_reduction;

    return HY_OK;
}

int32_t hy_abi_version(void) {
    return HY_ABI_VERSION;
}

}  // extern "C"
