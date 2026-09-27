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
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "core/rules.h"
#include "core/strutil.h"
#include "json.hpp"
#include "parse/song.h"
#include "search/pather.h"

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
        if (hydra::ends_with(p, suffix)) return p;
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

// ---- cached corpus work ---------------------------------------------------
// Many corpus loops parse the same chart, or analyze it at the same settings,
// as a loop in another test case. These two calls do each piece of work once
// per test run and hand every later caller the same answer. A failure is
// cached too: every call for that key throws the same exception again, so a
// loop's try/catch behaves exactly as it did around the direct call.
// Both return const references into caches that live for the whole run; a
// test that needs to change the Song or the record copies it first.

namespace detail {

template <class T>
struct Outcome {
    std::optional<T> value;
    std::exception_ptr error;
};

// Every SearchSettings field that can change a record.
inline void add_settings(std::ostringstream& k, const hydra::SearchSettings& s) {
    auto opt = [&k](const auto& o) {
        if (o) k << *o;
        else k << "none";
        k << '|';
    };
    k << s.sp_cap << '|';
    k << static_cast<int>(s.depth_mode) << '|' << s.depth_value << '|';
    opt(s.ms_filter);
    k << s.legacy_fill_deadline << '|' << s.rules.fingerprint() << '|';
}

}  // namespace detail

// The Song for one corpus chart, parsed once per run for these load options
// (the arguments load_songpath takes).
inline const hydra::Song& song(const std::string& path, bool pro, bool bass2x,
                               hydra::Difficulty difficulty = hydra::Difficulty::Expert,
                               const hydra::core::Rules& rules = hydra::core::default_rules()) {
    static std::map<std::string, detail::Outcome<hydra::Song>> cache;
    std::ostringstream key;
    key.precision(17);
    key << path << '|' << pro << '|' << bass2x << '|' << static_cast<int>(difficulty) << '|'
        << rules.fingerprint();
    auto [it, fresh] = cache.try_emplace(key.str());
    detail::Outcome<hydra::Song>& o = it->second;
    if (fresh) {
        try {
            o.value.emplace(hydra::load_songpath(path, pro, bass2x, difficulty, rules));
        } catch (...) {
            o.error = std::current_exception();
        }
    }
    if (o.error) std::rethrow_exception(o.error);
    return *o.value;
}

// One corpus chart analyzed under `settings`, once per run: the record
// analyze_chart_file would return (parsed with settings.prodrums, bass2x,
// difficulty and rules, then analyze_chart). Throws what that would throw.
inline const hydra::HydraRecord& analyzed(const std::string& path,
                                          const hydra::app::AnalysisSettings& settings) {
    static std::map<std::string, detail::Outcome<hydra::HydraRecord>> cache;
    std::ostringstream key;
    key.precision(17);
    key << path << '|' << settings.prodrums << '|' << settings.bass2x << '|'
        << static_cast<int>(settings.difficulty) << '|';
    detail::add_settings(key, settings);
    auto [it, fresh] = cache.try_emplace(key.str());
    detail::Outcome<hydra::HydraRecord>& o = it->second;
    if (fresh) {
        try {
            const hydra::Song& s = song(path, settings.prodrums, settings.bass2x,
                                        settings.difficulty, settings.rules);
            o.value.emplace(hydra::analyze_chart(s, settings));
        } catch (...) {
            o.error = std::current_exception();
        }
    }
    if (o.error) std::rethrow_exception(o.error);
    return *o.value;
}

// The same for a loop that holds plain SearchSettings and parses with
// load_songpath(path, true, true): pro drums, 2x bass, Expert.
inline const hydra::HydraRecord& analyzed(const std::string& path,
                                          const hydra::SearchSettings& settings) {
    hydra::app::AnalysisSettings a;
    static_cast<hydra::SearchSettings&>(a) = settings;
    return analyzed(path, a);
}

}  // namespace corpus

#endif  // HYDRA_TESTS_CORPUS_UTIL_H
