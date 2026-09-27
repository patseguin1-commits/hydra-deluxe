#include "app/display_format.h"

#include <cstdio>
#include <cstdlib>

namespace hydra::app {

double py_round3(double v) {
    // Python's round(x, 3) rounds the exact binary value to 3 decimal places,
    // ties-to-even. MSVC's printf does the same correctly-rounded conversion,
    // so format-and-reparse reproduces it.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return std::strtod(buf, nullptr);
}

std::string format_avg_mult(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", py_round3(v));
    return buf;
}

std::string format_ms(double ms) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1fms", ms);
    return buf;
}

std::string format_ms_spaced(double ms) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
    return buf;
}

}  // namespace hydra::app
