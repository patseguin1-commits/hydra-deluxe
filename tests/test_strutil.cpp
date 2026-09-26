// The one owner of Hydra's small string helpers (core/strutil). Every caller
// that used to carry its own lowercase, trim or suffix check reads these.

#include "doctest.h"

#include <string>

#include "core/strutil.h"

using namespace hydra;

TEST_CASE("strutil: to_lower_ascii lowers A-Z and leaves every other byte") {
    CHECK(to_lower_ascii("Notes.MID") == "notes.mid");
    CHECK(to_lower_ascii("AbCdEF0123") == "abcdef0123");
    // UTF-8 bytes pass through untouched: "ÉTÉ" becomes "ÉtÉ".
    CHECK(to_lower_ascii("\xC3\x89T\xC3\x89") == "\xC3\x89t\xC3\x89");
    CHECK(to_lower_ascii("") == "");
}

TEST_CASE("strutil: trim strips ASCII whitespace from both ends only") {
    CHECK(trim("  key = value \r\n") == "key = value");
    CHECK(trim("\t\v\fx y\f\v\t") == "x y");
    CHECK(trim(" \t\r\n") == "");
    CHECK(trim("") == "");
    CHECK(trim("inner  space") == "inner  space");
}

TEST_CASE("strutil: ends_with is exact and ends_with_ci ignores ASCII case") {
    CHECK(ends_with("song.mid", ".mid"));
    CHECK(ends_with(".mid", ".mid"));
    CHECK_FALSE(ends_with("song.MID", ".mid"));
    CHECK_FALSE(ends_with("mid", ".mid"));
    CHECK(ends_with_ci("SONG.Mid", ".mid"));
    CHECK(ends_with_ci("track.OPUS", ".opus"));
    CHECK_FALSE(ends_with_ci("track.opus.bak", ".opus"));
    CHECK_FALSE(ends_with_ci("s", ".sng"));
    CHECK(ends_with_ci("x", ""));
}
