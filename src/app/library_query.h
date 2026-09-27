// The library search box's rules, in one place: how text is folded for
// matching (case, accents, full-width letters, whitespace), how Clone Hero's
// rich-text tags are removed, how a typed query is read, whether a row
// matches it, and which bytes of a shown string to highlight. Pure functions
// with no ImGui and no store, so the library table and the song panel share
// them and the unit tests pin them.

#ifndef HYDRA_APP_LIBRARY_QUERY_H
#define HYDRA_APP_LIBRARY_QUERY_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::app {

// Lowercase, accents removed (NFD-style fold for Latin-1 and Latin Extended-A:
// "é" -> "e", "ß" -> "ss"), full-width ASCII folded to ASCII, runs of
// whitespace collapsed to one space. Non-Latin text (Japanese, etc.) passes
// through unchanged apart from full-width folding. Input and output are UTF-8.
// Whitespace includes the no-break space (U+00A0) and the ideographic space
// (U+3000). The signs × and ÷ are kept. Invalid UTF-8 passes through byte for
// byte; nothing is dropped and nothing throws.
std::string fold_for_search(std::string_view text);

// Removes Clone Hero rich-text tags: <color=...>, </color>, <b>, </b>, <i>,
// </i>, <size=...>, </size>, <u>, </u>, <s>, </s>, <sub>, </sub>, <sup>,
// </sup>, case-insensitive. Anything else in angle brackets is kept, including
// <color> with no value, a tag that never closes, and "<unknown artist>".
std::string strip_rich_tags(std::string_view text);

enum class QueryField { Any, Title, Artist, Charter, Folder };

struct QueryTerm {
    QueryField field = QueryField::Any;
    std::string folded;   // fold_for_search of the word or quoted phrase
    bool phrase = false;  // true when it came from "quotes"
};

struct LibraryQuery {
    std::vector<QueryTerm> terms;         // every term must match
    std::optional<int> stars;             // stars:N, N in 0..7
    std::optional<double> squeeze_max_ms; // squeeze<=N (also squeeze<N treated as <=)
    std::vector<std::string> errors;      // e.g. "stars: needs a number from 0 to 7"
    bool empty() const;                   // no terms and no filters
};

// Parses what the user typed. Words match in any order. "quoted text" is one
// phrase. artist:x, charter:x, folder:x, title:x limit a word or "phrase" to
// one field. stars:N and squeeze<=N filter on the stored best path. Unknown
// field names are treated as plain words.
// A word or phrase matches when its folded text appears anywhere in a folded
// field. An unclosed quote runs to the end. A field name with nothing after it
// is dropped. A bad filter value adds one sentence to `errors` and filters
// nothing. When a filter appears twice, the last one counts.
LibraryQuery parse_library_query(std::string_view text);

// The fields one library row offers to a query, already folded.
struct SearchableRow {
    std::string title, artist, charter, folder;  // fold_for_search(strip_rich_tags(x))
};
SearchableRow make_searchable(std::string_view title, std::string_view artist,
                              std::string_view charter, std::string_view folder);

// The best path's stored facts a filter can test; nullopt when not analyzed.
// A row counts as analyzed when `stars` holds a value.
struct RowFacts {
    std::optional<int> stars;
    std::optional<double> hardest_ms;  // nullopt = no squeeze on the path
};

// True when every term matches its field (or any field) and every filter holds.
// A stars: or squeeze filter never matches a row with no facts.
// A path with no squeeze passes any squeeze limit. Allocates nothing.
bool query_matches(const LibraryQuery& q, const SearchableRow& row, const RowFacts& facts);

// Where the query's terms appear in one displayed (unfolded, tag-stripped)
// string, as byte ranges, for highlighting. Ranges are sorted and don't overlap.
// Only terms for `field` or for any field count; QueryField::Any counts every
// term. A match covers every byte of each character it touches, so "beyonce"
// in "Beyoncé" covers both bytes of "é". Touching ranges merge.
struct MatchSpan { size_t begin = 0, end = 0; };
std::vector<MatchSpan> match_spans(const LibraryQuery& q, QueryField field,
                                   std::string_view display_text);

}  // namespace hydra::app

#endif  // HYDRA_APP_LIBRARY_QUERY_H
