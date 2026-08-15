// Tests for app/analysis.{h,cpp} — chord counting cross-checked against
// golden's `song` block (same tally, computed a different way), plus
// discovery/hashing sanity over the real test/input corpus (not gated by
// golden: these are filesystem-facing, not part of the algorithm spine).

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

#include "app/analysis.h"
#include "golden_util.h"

using namespace hydra::app;

namespace {

// Chart libraries routinely have non-ASCII paths (this corpus has one with a
// fullwidth slash); std::ifstream's narrow-string overload goes through the
// system codepage on Windows and mangles those, so round-trip through UTF-16
// like analysis.cpp's own file reads do.
bool file_exists_utf8(const std::string& utf8_path) {
    int wlen =
        MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(), (int)utf8_path.size(), nullptr, 0);
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(), (int)utf8_path.size(), &wpath[0], wlen);
    FILE* f = _wfopen(wpath.c_str(), L"rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

}  // namespace

TEST_CASE("count_chart_chords matches golden's song-block code tally") {
    const golden::json idx = golden::index();
    int checked = 0;

    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string ext = relpath.substr(relpath.find_last_of('.') + 1);
        if (ext != "mid" && ext != "chart") continue;  // count_chart_chords doesn't take .sng

        golden::json doc = golden::chart(entry["slug"].get<std::string>());
        std::map<std::string, int> expected;
        for (const auto& ev : doc["song"]["events"]) ++expected[ev["code"].get<std::string>()];

        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;
        std::map<std::string, int> actual = count_chart_chords(path);

        CHECK_MESSAGE(actual == expected, relpath);
        ++checked;
    }

    CHECK(checked > 0);
    MESSAGE("checked " << checked << " charts");
}

TEST_CASE("discover_charts walks the corpus without throwing") {
    auto [items, errors] = discover_charts({std::string(HYDRA_INPUT_DIR)});
    CHECK(errors.empty());

    for (const ScanItem& item : items) {
        CHECK_MESSAGE(file_exists_utf8(item.notespath), item.notespath);
        CHECK(item.md5.size() == 32);
        for (char c : item.md5)
            CHECK((std::isxdigit(static_cast<unsigned char>(c)) != 0));
    }

    MESSAGE("discovered " << items.size() << " chart(s)");
}

TEST_CASE("discover_charts returns nothing for an empty root list") {
    auto [items, errors] = discover_charts({});
    CHECK(items.empty());
    CHECK(errors.empty());
}

TEST_CASE("get_folder_count agrees with the folder count discover_charts visits") {
    int seen_by_discover = 0;
    discover_charts({std::string(HYDRA_INPUT_DIR)},
                    [&](int n) { seen_by_discover = n; });
    int counted = get_folder_count({std::string(HYDRA_INPUT_DIR)});
    CHECK(counted == seen_by_discover);
}
