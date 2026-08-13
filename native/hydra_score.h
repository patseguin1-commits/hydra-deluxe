// C ABI for Hydra's native scoring core. Kept in sync with hydra/hynative.py,
// which declares the same structs and signatures to ctypes.
//
// Bump HY_ABI_VERSION on any change to a struct layout or signature here.
// hynative.py checks it at load time and falls back to pure Python on a
// mismatch, so a stale DLL degrades to "slower" rather than "wrong".

#ifndef HYDRA_SCORE_H
#define HYDRA_SCORE_H

#include <cstdint>

// 2 adds the path search (hydra_search.h) alongside the scoring core.
#define HY_ABI_VERSION 2

// Per-note flags, packed one byte per note.
#define HY_NOTE_CYMBAL     0x01
#define HY_NOTE_ACCENT     0x02
#define HY_NOTE_GHOST      0x04
#define HY_NOTE_ACTIVATION 0x08

// Return codes.
#define HY_OK             0
#define HY_ERR_NULL_OUT   (-1)
#define HY_ERR_NULL_NOTES (-2)
#define HY_ERR_BAD_COUNT  (-3)

#ifdef _WIN32
#define HY_EXPORT __declspec(dllexport)
#else
#define HY_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

// Mirrors the dict returned by hypath.category_scores.
typedef struct hy_scores {
    int32_t base;
    int32_t combo;
    int32_t sp;
    int32_t accent;
    int32_t ghost;
    int32_t sqout_reduction;
    int32_t skipped_dynamic_reduction;
} hy_scores;

// note_flags must be base-sorted, as chord.notes(basesorted=True) returns.
HY_EXPORT int32_t hy_category_scores(const uint8_t* note_flags,
                                     int32_t note_count,
                                     int32_t combo,
                                     int32_t flag_skipped_dynamics,
                                     hy_scores* out);

HY_EXPORT int32_t hy_abi_version(void);

}  // extern "C"

#endif  // HYDRA_SCORE_H
