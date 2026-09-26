#include "parse/chart_files.h"

#include <string>

#include "core/strutil.h"

namespace hydra {

ChartFormat chart_format_of(std::string_view path) {
    if (ends_with_ci(path, ".mid")) return ChartFormat::Mid;
    if (ends_with_ci(path, ".chart")) return ChartFormat::Chart;
    if (ends_with_ci(path, ".sng")) return ChartFormat::Sng;
    if (ends_with_ci(path, ".srb")) return ChartFormat::Srb;
    return ChartFormat::None;
}

ChartFormat notes_file_format(std::string_view filename) {
    const std::string low = to_lower_ascii(filename);
    if (low == "notes.mid") return ChartFormat::Mid;
    if (low == "notes.chart") return ChartFormat::Chart;
    return ChartFormat::None;
}

bool is_song_ini(std::string_view filename) { return to_lower_ascii(filename) == "song.ini"; }

}  // namespace hydra
