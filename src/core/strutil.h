// Small, generic string helpers with no other natural home. Every caller in
// Hydra uses these rather than its own copy.

#ifndef HYDRA_CORE_STRUTIL_H
#define HYDRA_CORE_STRUTIL_H

#include <string>
#include <string_view>

namespace hydra {

// ASCII-only lowercase: A-Z become a-z and every other byte is left alone, so
// UTF-8 text passes through intact. Chart hashes, file names and ini keys all
// go through here.
std::string to_lower_ascii(std::string_view s);

// Strips ASCII whitespace (space, tab, CR, LF, vertical tab, form feed) from
// both ends. Inner whitespace is kept.
std::string trim(std::string_view s);

// Whether s ends with suffix, byte for byte.
bool ends_with(std::string_view s, std::string_view suffix);

// Whether s ends with suffix, ignoring ASCII case (".MID" matches ".mid").
bool ends_with_ci(std::string_view s, std::string_view suffix);

}  // namespace hydra

#endif  // HYDRA_CORE_STRUTIL_H
