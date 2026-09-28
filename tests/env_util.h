// Reads an environment variable for a test's dev aids (HYDRA_PREVIEW_DUMP,
// HYDRA_PAGE_SAMPLES, ...). std::getenv hands back a pointer into the
// environment that another thread could invalidate, which MSVC warns about
// (C4996); _dupenv_s copies the value out instead.

#ifndef HYDRA_TESTS_ENV_UTIL_H
#define HYDRA_TESTS_ENV_UTIL_H

#include <cstdlib>
#include <optional>
#include <string>

// The variable's value, or nullopt when it is not set.
inline std::optional<std::string> read_env(const char* name) {
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;
    std::string out(value);
    std::free(value);
    return out;
}

#endif  // HYDRA_TESTS_ENV_UTIL_H
