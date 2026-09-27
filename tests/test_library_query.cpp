// Unit tests for app/library_query: how the library search folds text,
// strips Clone Hero's rich-text tags, reads a query, matches a row and marks
// what to highlight. Burnout, Deadbolt and Acid Romance use their real
// metadata from testdata\input; every other row is made up.

#include "doctest.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/library_query.h"

using namespace hydra::app;

namespace {

bool matches(std::string_view query, const SearchableRow& row, const RowFacts& facts = {}) {
    return query_matches(parse_library_query(query), row, facts);
}

// The facts of an analyzed row: its best path's stars and hardest squeeze.
RowFacts analyzed(int stars, std::optional<double> hardest_ms) {
    RowFacts facts;
    facts.stars = stars;
    facts.hardest_ms = hardest_ms;
    return facts;
}

SearchableRow burnout() {
    return make_searchable("Burnout", "Green Day", "Hoph2o",
                           "common\\Summer Blast _25 Setlist\\Tier 4");
}

SearchableRow deadbolt() {
    return make_searchable("Deadbolt", "Thrice", "Hoph2o", "common\\IB24\\T3");
}

SearchableRow acid_romance() {
    return make_searchable("Acid Romance", "Alpha Wolf", "<color=#e02222>Blood</color>line",
                           "common\\IB24\\T3");
}

bool spans_equal(const std::vector<MatchSpan>& got, const std::vector<MatchSpan>& want) {
    return std::equal(got.begin(), got.end(), want.begin(), want.end(),
                      [](const MatchSpan& a, const MatchSpan& b) {
                          return a.begin == b.begin && a.end == b.end;
                      });
}

}  // namespace

TEST_CASE("library query: folding lowercases and removes accents") {
    CHECK(fold_for_search("Beyoncé") == "beyonce");
    CHECK(fold_for_search("ÀÉÎÕÜ Ñ Ç Ý Ÿ") == "aeiou n c y y");
    CHECK(fold_for_search("Straße") == "strasse");
    CHECK(fold_for_search("Æther Œuvre Þorn") == "aether oeuvre thorn");
    CHECK(fold_for_search("Łódź Škoda Ğİı") == "lodz skoda gii");
    CHECK(fold_for_search("a × b ÷ c") == "a × b ÷ c");  // signs, not letters
}

TEST_CASE("library query: folding turns full-width ASCII into ASCII") {
    CHECK(fold_for_search("ＢＵＲＮＯＵＴ！") == "burnout!");
    // Full-width "tier", the ideographic space U+3000, full-width "4".
    CHECK(fold_for_search("ｔｉｅｒ" "\xE3\x80\x80" "４") == "tier 4");
}

TEST_CASE("library query: folding collapses whitespace and keeps other scripts") {
    CHECK(fold_for_search("Tier  4\t\tSong") == "tier 4 song");
    CHECK(fold_for_search("a" "\xC2\xA0\xC2\xA0" "b") == "a b");  // two no-break spaces
    CHECK(fold_for_search("東京事変") == "東京事変");
    CHECK(fold_for_search("") == "");
}

TEST_CASE("library query: invalid UTF-8 passes through byte for byte") {
    CHECK(fold_for_search("A" "\xC3" "(B") == "a" "\xC3" "(b");  // lead byte, no continuation
    CHECK(fold_for_search("\xFF\xFE") == "\xFF\xFE");
    CHECK(fold_for_search("x" "\xC3") == "x" "\xC3");  // cut off at the end
    CHECK(fold_for_search("\xEF\xBC") == "\xEF\xBC");  // half a full-width letter
}

TEST_CASE("library query: rich-text tags are stripped and other angle brackets kept") {
    CHECK(strip_rich_tags("<color=#e02222>Blood</color>line") == "Bloodline");
    CHECK(strip_rich_tags("<COLOR=red>Loud</Color>") == "Loud");
    CHECK(strip_rich_tags("<b>Bold</b> <i>it</i> <u>u</u> <s>s</s>") == "Bold it u s");
    CHECK(strip_rich_tags("<size=20>big</size> H<sub>2</sub>O x<sup>2</sup>") == "big H2O x2");
    CHECK(strip_rich_tags("<unknown artist>") == "<unknown artist>");
    CHECK(strip_rich_tags("a < b > c") == "a < b > c");
    CHECK(strip_rich_tags("<color=#fff") == "<color=#fff");  // never closed
    CHECK(strip_rich_tags("") == "");
}

TEST_CASE("library query: a tagged charter is found by its plain name") {
    const SearchableRow row = acid_romance();
    CHECK(row.charter == "bloodline");
    CHECK(matches("bloodline", row));
    CHECK(matches("charter:blood", row));
    CHECK_FALSE(matches("e02222", row));  // the tag's text is gone
    CHECK_FALSE(matches("color", row));
}

TEST_CASE("library query: an accented name is found without its accents") {
    const SearchableRow row = make_searchable("Halo", "Beyoncé", "Someone", "pack");  // made up
    CHECK(matches("beyonce", row));
    CHECK(matches("BEYONCE", row));
    CHECK(matches("beyoncé", row));
}

TEST_CASE("library query: words match in any order and across fields") {
    const SearchableRow row = burnout();
    CHECK(matches("green burnout", row));
    CHECK(matches("burnout green", row));
    CHECK(matches("  Green   BURNOUT  ", row));
    CHECK(matches("hoph2o tier", row));  // charter and folder
    CHECK_FALSE(matches("green deadbolt", row));
    CHECK_FALSE(matches("thrice burnout", row));
}

TEST_CASE("library query: a quoted phrase must appear whole in one field") {
    const LibraryQuery q = parse_library_query("\"tier 4\"");
    REQUIRE(q.terms.size() == 1);
    CHECK(q.terms[0].phrase);
    CHECK(q.terms[0].field == QueryField::Any);
    CHECK(q.terms[0].folded == "tier 4");

    const RowFacts none;
    CHECK(query_matches(q, burnout(), none));
    CHECK_FALSE(query_matches(q, make_searchable("Song", "Band", "Charter", "common\\IB24\\T4"),
                              none));  // made up

    // As two words, "tier" and "4" may sit apart. As a phrase they may not.
    const SearchableRow apart = make_searchable("Song 4", "Band", "Charter", "Pack\\Tier 1");  // made up
    CHECK(matches("tier 4", apart));
    CHECK_FALSE(matches("\"tier 4\"", apart));
    // A phrase can't be split across two fields either.
    CHECK_FALSE(matches("\"burnout green\"", burnout()));
    // A quote that is never closed runs to the end.
    CHECK(matches("\"summer blast", burnout()));
}

TEST_CASE("library query: artist: charter: folder: and title: limit a term to one field") {
    CHECK(matches("artist:thrice", deadbolt()));
    CHECK_FALSE(matches("artist:thrice", burnout()));
    CHECK_FALSE(matches("title:thrice", deadbolt()));
    CHECK(matches("charter:hoph2o", burnout()));
    CHECK(matches("charter:hoph2o", deadbolt()));
    CHECK_FALSE(matches("charter:hoph2o", acid_romance()));
    CHECK(matches("folder:ib24", deadbolt()));
    CHECK_FALSE(matches("folder:ib24", burnout()));
    CHECK(matches("title:burnout", burnout()));
    CHECK(matches("Artist:\"green day\"", burnout()));
    CHECK_FALSE(matches("title:\"green day\"", burnout()));

    const LibraryQuery q = parse_library_query("ARTIST:\"Green Day\" folder:tier");
    REQUIRE(q.terms.size() == 2);
    CHECK(q.terms[0].field == QueryField::Artist);
    CHECK(q.terms[0].folded == "green day");
    CHECK(q.terms[0].phrase);
    CHECK(q.terms[1].field == QueryField::Folder);
    CHECK(q.terms[1].folded == "tier");
    CHECK_FALSE(q.terms[1].phrase);
}

TEST_CASE("library query: unknown field names are words and empty field values are dropped") {
    const LibraryQuery unknown = parse_library_query("genre:rock");
    REQUIRE(unknown.terms.size() == 1);
    CHECK(unknown.terms[0].field == QueryField::Any);
    CHECK(unknown.terms[0].folded == "genre:rock");
    CHECK(unknown.errors.empty());

    // A field name with nothing after it is dropped, so the list doesn't
    // empty out while the user is still typing.
    const LibraryQuery typing = parse_library_query("artist:");
    CHECK(typing.terms.empty());
    CHECK(typing.empty());
}

TEST_CASE("library query: stars:N keeps analyzed rows with exactly N stars") {
    const LibraryQuery q = parse_library_query("stars:7");
    REQUIRE(q.stars.has_value());
    CHECK(*q.stars == 7);
    CHECK(q.errors.empty());
    CHECK(query_matches(q, burnout(), analyzed(7, 163.0)));
    CHECK_FALSE(query_matches(q, burnout(), analyzed(6, 163.0)));
    CHECK_FALSE(query_matches(q, burnout(), RowFacts{}));  // not analyzed
    CHECK(matches("stars:0", burnout(), analyzed(0, std::nullopt)));
    CHECK(matches("green stars:7", burnout(), analyzed(7, 163.0)));
    CHECK_FALSE(matches("thrice stars:7", burnout(), analyzed(7, 163.0)));
}

TEST_CASE("library query: squeeze<=N keeps analyzed rows whose hardest squeeze is at most N ms") {
    const LibraryQuery q = parse_library_query("squeeze<=20");
    REQUIRE(q.squeeze_max_ms.has_value());
    CHECK(*q.squeeze_max_ms == doctest::Approx(20.0));
    CHECK(query_matches(q, burnout(), analyzed(7, 12.5)));
    CHECK(query_matches(q, burnout(), analyzed(7, 20.0)));
    CHECK_FALSE(query_matches(q, burnout(), analyzed(7, 163.0)));   // Burnout's real 163.0 ms
    CHECK(query_matches(q, burnout(), analyzed(7, std::nullopt)));  // no squeeze at all
    CHECK_FALSE(query_matches(q, burnout(), RowFacts{}));           // not analyzed

    CHECK(matches("squeeze<=163", burnout(), analyzed(7, 163.0)));
    CHECK(matches("squeeze<20", burnout(), analyzed(7, 20.0)));  // < reads as <=
    CHECK(matches("SQUEEZE<=20", burnout(), analyzed(7, 20.0)));
    CHECK(matches("squeeze<=-5", burnout(), analyzed(7, -12.0)));
    CHECK(matches("squeeze<=2.5", burnout(), analyzed(7, 2.5)));
}

TEST_CASE("library query: bad filter values give a plain error and filter nothing") {
    const LibraryQuery stars = parse_library_query("burnout stars:9");
    CHECK_FALSE(stars.stars.has_value());
    REQUIRE(stars.errors.size() == 1);
    CHECK(stars.errors[0] == "stars: needs a number from 0 to 7");
    REQUIRE(stars.terms.size() == 1);  // the rest of the query still works
    CHECK(stars.terms[0].folded == "burnout");
    CHECK(query_matches(stars, burnout(), RowFacts{}));

    CHECK(parse_library_query("stars:x").errors ==
          std::vector<std::string>{"stars: needs a number from 0 to 7"});
    CHECK(parse_library_query("stars:-1").errors.size() == 1);
    CHECK(parse_library_query("stars:").errors.size() == 1);

    const LibraryQuery squeeze = parse_library_query("squeeze<=abc");
    CHECK_FALSE(squeeze.squeeze_max_ms.has_value());
    REQUIRE(squeeze.errors.size() == 1);
    CHECK(squeeze.errors[0] == "squeeze<= needs a number of milliseconds, like squeeze<=20");
    CHECK(parse_library_query("squeeze<=").errors.size() == 1);
    CHECK(parse_library_query("squeeze<=20ms").errors.size() == 1);

    // The same mistake twice is reported once.
    CHECK(parse_library_query("stars:9 stars:10").errors.size() == 1);
}

TEST_CASE("library query: an empty query matches every row") {
    for (std::string_view text : {"", "   ", "\"\"", "\" \"", "title:"}) {
        const LibraryQuery q = parse_library_query(text);
        CHECK(q.empty());
        CHECK(q.errors.empty());
        CHECK(query_matches(q, burnout(), RowFacts{}));
        CHECK(query_matches(q, acid_romance(), analyzed(3, 40.0)));
    }
    CHECK_FALSE(parse_library_query("x").empty());
    CHECK_FALSE(parse_library_query("stars:7").empty());
    CHECK_FALSE(parse_library_query("squeeze<=20").empty());
}

TEST_CASE("library query: highlight spans cover the displayed bytes") {
    // "é" is two bytes, so the span runs to byte 14, not 13.
    const std::string title = "Halo (Beyoncé cover)";  // made up
    CHECK(spans_equal(match_spans(parse_library_query("beyonce"), QueryField::Title, title),
                      std::vector<MatchSpan>{MatchSpan{6, 14}}));
    CHECK(title.substr(6, 8) == "Beyoncé");

    // The caller passes the tag-stripped charter, as the table shows it.
    const std::string charter = strip_rich_tags("<color=#e02222>Blood</color>line");
    CHECK(spans_equal(match_spans(parse_library_query("bloodline"), QueryField::Charter, charter),
                      std::vector<MatchSpan>{MatchSpan{0, 9}}));

    // A phrase in the folder, despite the capital T.
    const std::string folder = "common\\Summer Blast _25 Setlist\\Tier 4";
    CHECK(spans_equal(match_spans(parse_library_query("\"tier 4\""), QueryField::Folder, folder),
                      std::vector<MatchSpan>{MatchSpan{32, 38}}));

    // Two spaces fold to one, and the span still covers both.
    CHECK(spans_equal(match_spans(parse_library_query("\"tier 4\""), QueryField::Folder, "Tier  4"),
                      std::vector<MatchSpan>{MatchSpan{0, 7}}));

    // Every occurrence lights up.
    CHECK(spans_equal(match_spans(parse_library_query("tier"), QueryField::Folder, "Tier 1\\Tier 2"),
                      std::vector<MatchSpan>{MatchSpan{0, 4}, MatchSpan{7, 11}}));

    // Touching matches merge. A field-limited term lights up only its field.
    const LibraryQuery words = parse_library_query("burn out artist:green");
    CHECK(spans_equal(match_spans(words, QueryField::Title, "Burnout"),
                      std::vector<MatchSpan>{MatchSpan{0, 7}}));
    CHECK(spans_equal(match_spans(words, QueryField::Artist, "Green Day"),
                      std::vector<MatchSpan>{MatchSpan{0, 5}}));
    CHECK(match_spans(parse_library_query("artist:green"), QueryField::Title, "Green Light").empty());
}

TEST_CASE("library query: matching 20000 rows takes well under a frame") {
    std::vector<SearchableRow> rows;  // made up, library-shaped
    rows.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        rows.push_back(make_searchable(
            "Song number " + std::to_string(i), "Artist " + std::to_string(i % 500),
            "Charter " + std::to_string(i % 50),
            "common\\Pack " + std::to_string(i % 40) + "\\Tier " + std::to_string(i % 8)));
    }
    const LibraryQuery q = parse_library_query("artist 12 \"tier 3\"");
    const RowFacts none;

    // Best of three, so a busy machine doesn't fail the test.
    double best_ms = 1e9;
    size_t hits = 0;
    for (int run = 0; run < 3; ++run) {
        const auto start = std::chrono::steady_clock::now();
        hits = 0;
        for (const SearchableRow& row : rows)
            if (query_matches(q, row, none)) ++hits;
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;
        best_ms = std::min(best_ms, took.count());
    }
    CHECK(hits > 0);
    CHECK(best_ms < 20.0);
}
