// Which file is a chart, decided once for the library scan and the parser.
//
// Clone Hero reads a loose folder's notes.mid / notes.chart / song.ini in any
// letter case, and so does Hydra: the scan (app/analysis.cpp discover_charts)
// and the loaders (parse/song.cpp) both ask these functions, so the two can
// never disagree about which files count.

#ifndef HYDRA_PARSE_CHART_FILES_H
#define HYDRA_PARSE_CHART_FILES_H

#include <string_view>

namespace hydra {

enum class ChartFormat { None, Mid, Chart, Sng, Srb };

// A path's chart format from its extension, any case: ".mid", ".chart",
// ".sng", ".srb". Anything else is None.
ChartFormat chart_format_of(std::string_view path);

// A loose folder's notes file by its exact name, any case: "notes.mid" is
// Mid, "notes.chart" is Chart, anything else is None.
ChartFormat notes_file_format(std::string_view filename);

// "song.ini", any case.
bool is_song_ini(std::string_view filename);

}  // namespace hydra

#endif  // HYDRA_PARSE_CHART_FILES_H
