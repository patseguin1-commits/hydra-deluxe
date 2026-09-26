#include "core/strutil.h"

namespace hydra {

namespace {

constexpr std::string_view kSpace = " \t\r\n\v\f";

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

std::string trim(std::string_view s) {
    const size_t a = s.find_first_not_of(kSpace);
    if (a == std::string_view::npos) return std::string();
    const size_t b = s.find_last_not_of(kSpace);
    return std::string(s.substr(a, b - a + 1));
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::string_view tail = s.substr(s.size() - suffix.size());
    for (size_t i = 0; i < suffix.size(); ++i)
        if (lower_ascii(tail[i]) != lower_ascii(suffix[i])) return false;
    return true;
}

}  // namespace hydra
