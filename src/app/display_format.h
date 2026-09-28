// The display text for numbers that more than one screen shows. Each rule
// lives here once, so Song Details and the report cannot drift apart.

#ifndef HYDRA_APP_DISPLAY_FORMAT_H
#define HYDRA_APP_DISPLAY_FORMAT_H

#include <string>

namespace hydra::app {

// round(v, 3), matching Python's correctly-rounded decimal rounding.
double py_round3(double v);

// The average multiplier as every screen shows it: py_round3, three places.
std::string format_avg_mult(double v);

// A timing in ms, one decimal, with the unit: "12.3ms". The caller decides
// the sign; the early fill passes Activation::e_difficulty(true), which
// is positive when the fill is hit early.
std::string format_ms(double ms);

// A timing in ms for a sentence or a label: one decimal, a space, the unit
// ("163.0 ms"). The new Paths and Preview text uses this; format_ms keeps the
// older "163.0ms" form the report and the backend table print.
std::string format_ms_spaced(double ms);

}  // namespace hydra::app

#endif  // HYDRA_APP_DISPLAY_FORMAT_H
