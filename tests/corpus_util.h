// Helpers for iterating the checked-in chart corpus (testdata/input) from the
// C++ tests. The corpus is enumerated with app::discover_charts — the same
// discovery the app's library scan uses — so the tests exercise exactly the
// set of charts a user's scan of this tree would find.
//
// (This header replaced tests/golden_util.h when the golden oracle data was
// removed; the tests assert structural invariants and self-consistency now,
// not byte parity against stored output.)

#ifndef HYDRA_TESTS_CORPUS_UTIL_H
#define HYDRA_TESTS_CORPUS_UTIL_H

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "json.hpp"

#ifndef HYDRA_INPUT_DIR
#error "HYDRA_INPUT_DIR must be defined (see CMakeLists.txt)"
#endif

namespace corpus {

using json = nlohmann::json;

inline std::string root() { return HYDRA_INPUT_DIR; }

// Every chart file in the corpus (full notespath), sorted for a stable
// iteration order. Discovery errors throw: the corpus is checked in, so any
// error means a broken tree, not an environment problem.
inline const std::vector<std::string>& chart_paths() {
    static const std::vector<std::string> paths = [] {
        auto [items, errors] = hydra::app::discover_charts({root()});
        if (!errors.empty())
            throw std::runtime_error("corpus discovery error: " + errors.front());
        std::vector<std::string> out;
        out.reserve(items.size());
        for (const auto& item : items) out.push_back(item.notespath);
        std::sort(out.begin(), out.end());
        return out;
    }();
    return paths;
}

// First corpus chart whose path ends in `suffix` (e.g. ".mid").
inline std::string first_chart_with_suffix(const std::string& suffix) {
    for (const std::string& p : chart_paths()) {
        if (p.size() > suffix.size() &&
            p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0)
            return p;
    }
    throw std::runtime_error("no corpus chart ends in " + suffix);
}

inline std::string read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline json load_json(const std::string& path) {
    return json::parse(read_bytes(path));
}

}  // namespace corpus

#endif  // HYDRA_TESTS_CORPUS_UTIL_H
