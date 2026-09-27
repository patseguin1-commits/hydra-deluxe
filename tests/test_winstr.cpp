// Command-line arguments reach every entry point as UTF-8. main()'s char**
// argv is the ANSI copy, where Windows swaps a fullwidth slash for '/', so
// "Sugar<U+FF0F>Tzu" named a folder that does not exist; these pin that the split
// keeps each character as it was typed.

#include "doctest.h"

#include <string>
#include <vector>

#include "core/winstr.h"

TEST_CASE("split_command_line_utf8 keeps a fullwidth slash in a chart path") {
    const std::vector<std::string> args = hydra::split_command_line_utf8(
        L"hydra_replay.exe score --chart "
        L"\"C:\\songs\\black midi - Sugar\uFF0FTzu (Smoochums, Vasasasasa)\\notes.mid\"");
    REQUIRE(args.size() == 4);
    CHECK(args[0] == "hydra_replay.exe");
    CHECK(args[1] == "score");
    CHECK(args[2] == "--chart");
    // U+FF0F as UTF-8, quotes gone, spaces and comma kept inside the one argument.
    CHECK(args[3] ==
          "C:\\songs\\black midi - Sugar\xEF\xBC\x8FTzu (Smoochums, Vasasasasa)\\notes.mid");
    CHECK(args[3].find('/') == std::string::npos);
}

TEST_CASE("split_command_line_utf8 hands back UTF-8, not the ANSI code page") {
    // e-acute exists in code page 1252 (as byte E9), so the ANSI argv kept it
    // but in the wrong encoding for the UTF-8 path helpers; the kanji does not
    // exist there at all.
    const std::vector<std::string> args =
        hydra::split_command_line_utf8(L"hydra_batch.exe --db C:\\x\\caf\u00E9.db \u66F2");
    REQUIRE(args.size() == 4);
    CHECK(args[2] == "C:\\x\\caf\xC3\xA9.db");
    CHECK(args[3] == "\xE6\x9B\xB2");
}

TEST_CASE("split_command_line_utf8 of an empty command line is empty") {
    // CommandLineToArgvW alone would answer with the exe's own path.
    CHECK(hydra::split_command_line_utf8(L"").empty());
}

TEST_CASE("utf8_argv reads this process's command line") {
    const std::vector<std::string> args = hydra::utf8_argv();
    REQUIRE_FALSE(args.empty());
    CHECK(args[0].find("hydra_tests") != std::string::npos);
}
