// A tiny .chart carrying four different difficulty sections, shared by the
// parser tests and the container (.sng/.srb) pass-through tests.
//
// Expert, Hard and Easy are deliberately different from each other, and there
// is no MediumDrums section at all, so a test can tell which section the
// parser actually read. Hard also carries an SP phrase, an activation fill, a
// ghost and a cymbal marker — the things that live inside a difficulty section
// and have to follow it for free.

#ifndef HYDRA_TESTS_MULTIDIFF_CHART_H
#define HYDRA_TESTS_MULTIDIFF_CHART_H

#include <cstdint>
#include <string>
#include <vector>

namespace multidiff {

inline const char* chart_text() {
    return
        "[Song]\n"
        "{\n"
        "  Name = \"Difficulty Fixture\"\n"
        "  Artist = \"Hydra Tests\"\n"
        "  Resolution = 192\n"
        "}\n"
        "[SyncTrack]\n"
        "{\n"
        "  0 = TS 4\n"
        "  0 = B 120000\n"
        "}\n"
        "[ExpertDrums]\n"
        "{\n"
        "  0 = N 0 0\n"
        "  192 = N 1 0\n"
        "  384 = N 2 0\n"
        "  576 = N 3 0\n"
        "  768 = N 4 0\n"
        "}\n"
        "[HardDrums]\n"
        "{\n"
        "  0 = S 2 192\n"
        "  0 = N 1 0\n"
        "  0 = N 40 0\n"
        "  192 = N 2 0\n"
        "  192 = N 66 0\n"
        "  1536 = S 64 192\n"
        "  1728 = N 0 0\n"
        "}\n"
        "[EasyDrums]\n"
        "{\n"
        "  0 = N 4 0\n"
        "}\n";
}

// Chord counts the four difficulties must produce. Medium has no section.
inline constexpr size_t kExpertChords = 5;
inline constexpr size_t kHardChords = 3;
inline constexpr size_t kMediumChords = 0;
inline constexpr size_t kEasyChords = 1;

inline std::vector<uint8_t> chart_bytes() {
    std::string s = chart_text();
    return std::vector<uint8_t>(s.begin(), s.end());
}

}  // namespace multidiff

#endif  // HYDRA_TESTS_MULTIDIFF_CHART_H
