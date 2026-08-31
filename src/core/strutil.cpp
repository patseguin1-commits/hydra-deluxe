#include "core/strutil.h"

#include <cctype>

namespace hydra {

std::string lower_hex(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace hydra
