// hydra_rules.ini: the user's rule choices (core/rules.h) as flat
// "key = value" lines. No [section] header. A # starts a comment anywhere on
// a line. A missing file or key keeps the default. A bad value or an unknown
// key is an error that names the key, so a typo can never silently run the
// defaults.

#ifndef HYDRA_APP_RULES_FILE_H
#define HYDRA_APP_RULES_FILE_H

#include <filesystem>
#include <stdexcept>

#include "core/rules.h"

namespace hydra::app {

struct RulesFileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

core::Rules load_rules_file(const std::filesystem::path& path);

// exe_dir()\hydra_rules.ini, or PathOverrides::rules_path when set.
std::filesystem::path default_rules_path();

}  // namespace hydra::app

#endif  // HYDRA_APP_RULES_FILE_H
