#include "parse/chart_files.h"

#include <string>

namespace hydra {

namespace {

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool ends_with(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

ChartFormat chart_format_of(std::string_view path) {
    const std::string low = ascii_lower(path);
    if (ends_with(low, ".mid")) return ChartFormat::Mid;
    if (ends_with(low, ".chart")) return ChartFormat::Chart;
    if (ends_with(low, ".sng")) return ChartFormat::Sng;
    if (ends_with(low, ".srb")) return ChartFormat::Srb;
    return ChartFormat::None;
}

ChartFormat notes_file_format(std::string_view filename) {
    const std::string low = ascii_lower(filename);
    if (low == "notes.mid") return ChartFormat::Mid;
    if (low == "notes.chart") return ChartFormat::Chart;
    return ChartFormat::None;
}

bool is_song_ini(std::string_view filename) { return ascii_lower(filename) == "song.ini"; }

}  // namespace hydra
