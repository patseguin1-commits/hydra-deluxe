// Small, generic string helpers with no other natural home.

#ifndef HYDRA_CORE_STRUTIL_H
#define HYDRA_CORE_STRUTIL_H

#include <string>

namespace hydra {

// Lowercases a hex string (e.g. a chart hash) so two differently-cased
// spellings of the same value compare equal.
std::string lower_hex(std::string s);

}  // namespace hydra

#endif  // HYDRA_CORE_STRUTIL_H
