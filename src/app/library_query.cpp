// Library search: folding, rich-tag stripping, the query language, the
// matcher and the highlight spans. library_query.h describes the rules.

#include "app/library_query.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include "core/stars.h"  // kMaxStars

namespace hydra::app {

namespace {

constexpr size_t npos = std::string_view::npos;

constexpr const char* kStarsError = "stars: needs a number from 0 to 7";
constexpr const char* kSqueezeError =
    "squeeze<= needs a number of milliseconds, like squeeze<=20";

// ---- bytes -----------------------------------------------------------------

// The byte at s[i], or 0 past the end. 0 is never a continuation byte, so a
// cut-off sequence simply fails to match.
unsigned char byte_at(std::string_view s, size_t i) {
    return i < s.size() ? static_cast<unsigned char>(s[i]) : 0;
}

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

char ascii_lower(unsigned char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(static_cast<unsigned char>(a[i])) !=
            ascii_lower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals_ascii(s.substr(0, prefix.size()), prefix);
}

// ---- folding ---------------------------------------------------------------

// ASCII for U+00C0..U+00FF. nullptr keeps the character (the two signs).
constexpr const char* kLatin1[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c",     // C0  À Á Â Ã Ä Å Æ Ç
    "e", "e", "e", "e", "i", "i", "i",  "i",     // C8  È É Ê Ë Ì Í Î Ï
    "d", "n", "o", "o", "o", "o", "o",  nullptr, // D0  Ð Ñ Ò Ó Ô Õ Ö ×
    "o", "u", "u", "u", "u", "y", "th", "ss",    // D8  Ø Ù Ú Û Ü Ý Þ ß
    "a", "a", "a", "a", "a", "a", "ae", "c",     // E0  à á â ã ä å æ ç
    "e", "e", "e", "e", "i", "i", "i",  "i",     // E8  è é ê ë ì í î ï
    "d", "n", "o", "o", "o", "o", "o",  nullptr, // F0  ð ñ ò ó ô õ ö ÷
    "o", "u", "u", "u", "u", "y", "th", "y",     // F8  ø ù ú û ü ý þ ÿ
};

// ASCII for U+0100..U+017F, Latin Extended-A. Every entry is a letter.
constexpr const char* kLatinExtA[128] = {
    "a", "a", "a",  "a",  "a", "a", "c", "c",  // 0100  Ā ā Ă ă Ą ą Ć ć
    "c", "c", "c",  "c",  "c", "c", "d", "d",  // 0108  Ĉ ĉ Ċ ċ Č č Ď ď
    "d", "d", "e",  "e",  "e", "e", "e", "e",  // 0110  Đ đ Ē ē Ĕ ĕ Ė ė
    "e", "e", "e",  "e",  "g", "g", "g", "g",  // 0118  Ę ę Ě ě Ĝ ĝ Ğ ğ
    "g", "g", "g",  "g",  "h", "h", "h", "h",  // 0120  Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ
    "i", "i", "i",  "i",  "i", "i", "i", "i",  // 0128  Ĩ ĩ Ī ī Ĭ ĭ Į į
    "i", "i", "ij", "ij", "j", "j", "k", "k",  // 0130  İ ı Ĳ ĳ Ĵ ĵ Ķ ķ
    "k", "l", "l",  "l",  "l", "l", "l", "l",  // 0138  ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
    "l", "l", "l",  "n",  "n", "n", "n", "n",  // 0140  ŀ Ł ł Ń ń Ņ ņ Ň
    "n", "n", "n",  "n",  "o", "o", "o", "o",  // 0148  ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
    "o", "o", "oe", "oe", "r", "r", "r", "r",  // 0150  Ő ő Œ œ Ŕ ŕ Ŗ ŗ
    "r", "r", "s",  "s",  "s", "s", "s", "s",  // 0158  Ř ř Ś ś Ŝ ŝ Ş ş
    "s", "s", "t",  "t",  "t", "t", "t", "t",  // 0160  Š š Ţ ţ Ť ť Ŧ ŧ
    "u", "u", "u",  "u",  "u", "u", "u", "u",  // 0168  Ũ ũ Ū ū Ŭ ŭ Ů ů
    "u", "u", "u",  "u",  "w", "w", "y", "y",  // 0170  Ű ű Ų ų Ŵ ŵ Ŷ ŷ
    "y", "z", "z",  "z",  "z", "z", "z", "s",  // 0178  Ÿ Ź ź Ż ż Ž ž ſ
};

// The shown-text bytes one folded byte came from: the whole character, or the
// whole whitespace run, that produced it.
struct SourceRange {
    size_t begin = 0;
    size_t end = 0;
};

// Folds `text` into `out`. When `map` is given, it gets one entry per byte of
// `out`, so a match in the folded text can be traced back to the shown text.
void fold_into(std::string_view text, std::string& out, std::vector<SourceRange>* map) {
    out.clear();
    out.reserve(text.size());
    if (map) {
        map->clear();
        map->reserve(text.size());
    }
    auto emit = [&](std::string_view piece, size_t begin, size_t end) {
        out.append(piece);
        if (map)
            for (size_t k = 0; k < piece.size(); ++k) map->push_back(SourceRange{begin, end});
    };

    bool in_space = false;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c0 = byte_at(text, i);
        const unsigned char c1 = byte_at(text, i + 1);
        const unsigned char c2 = byte_at(text, i + 2);

        // Whitespace: ASCII, the no-break space (C2 A0) and the ideographic
        // space (E3 80 80). A run of any mix becomes one space.
        size_t space_len = 0;
        if (is_ascii_space(c0)) space_len = 1;
        else if (c0 == 0xC2 && c1 == 0xA0) space_len = 2;
        else if (c0 == 0xE3 && c1 == 0x80 && c2 == 0x80) space_len = 3;
        if (space_len > 0) {
            if (!in_space) emit(" ", i, i + space_len);
            else if (map) map->back().end = i + space_len;
            in_space = true;
            i += space_len;
            continue;
        }
        in_space = false;

        if (c0 < 0x80) {
            const char lower = ascii_lower(c0);
            emit(std::string_view(&lower, 1), i, i + 1);
            i += 1;
            continue;
        }

        // U+00C0..U+017F are the two-byte sequences C3 80 to C5 BF.
        if (c0 >= 0xC3 && c0 <= 0xC5 && is_continuation(c1)) {
            const unsigned cp = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            const char* ascii = cp <= 0xFF ? kLatin1[cp - 0xC0] : kLatinExtA[cp - 0x100];
            if (ascii) emit(ascii, i, i + 2);
            else emit(text.substr(i, 2), i, i + 2);
            i += 2;
            continue;
        }

        // Full-width ASCII, U+FF01..U+FF5E, is EF BC 81 to EF BD 9E. Its ASCII
        // twin is 0xFEE0 lower.
        if (c0 == 0xEF && (c1 == 0xBC || c1 == 0xBD) && is_continuation(c2)) {
            const unsigned cp = 0xF000u | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            if (cp >= 0xFF01 && cp <= 0xFF5E) {
                const char lower = ascii_lower(static_cast<unsigned char>(cp - 0xFEE0));
                emit(std::string_view(&lower, 1), i, i + 3);
                i += 3;
                continue;
            }
        }

        // Anything else is kept: the whole character when the bytes form a
        // well-shaped UTF-8 sequence, otherwise this one byte as it is.
        size_t len = 1;
        if (c0 >= 0xC2 && c0 <= 0xDF) len = 2;
        else if (c0 >= 0xE0 && c0 <= 0xEF) len = 3;
        else if (c0 >= 0xF0 && c0 <= 0xF4) len = 4;
        for (size_t k = 1; k < len; ++k)
            if (!is_continuation(byte_at(text, i + k))) len = 1;
        emit(text.substr(i, len), i, i + len);
        i += len;
    }
}

// ---- rich-text tags --------------------------------------------------------

struct RichTag {
    std::string_view name;
    bool takes_value;  // the opening tag is <name=value>
};

constexpr RichTag kRichTags[] = {
    {"color", true}, {"size", true}, {"b", false},   {"i", false},
    {"u", false},    {"s", false},   {"sub", false}, {"sup", false},
};

// The byte length of the rich-text tag that starts at text[at] (a '<'), or 0
// when the text there is not one strip_rich_tags removes.
size_t rich_tag_length(std::string_view text, size_t at) {
    size_t i = at + 1;
    const bool closing = i < text.size() && text[i] == '/';
    if (closing) ++i;
    size_t name_end = i;
    while (name_end < text.size() && is_ascii_alpha(text[name_end])) ++name_end;
    if (name_end == i || name_end >= text.size()) return 0;

    const std::string_view name = text.substr(i, name_end - i);
    for (const RichTag& tag : kRichTags) {
        if (!iequals_ascii(name, tag.name)) continue;
        if (text[name_end] == '>')
            return (closing || !tag.takes_value) ? name_end + 1 - at : 0;
        if (!closing && tag.takes_value && text[name_end] == '=') {
            const size_t close = text.find('>', name_end);
            const size_t reopen = text.find('<', name_end);
            if (close == npos || reopen < close) return 0;
            return close + 1 - at;
        }
        return 0;
    }
    return 0;
}

// ---- parsing ---------------------------------------------------------------

std::optional<QueryField> field_named(std::string_view key) {
    if (iequals_ascii(key, "title")) return QueryField::Title;
    if (iequals_ascii(key, "artist")) return QueryField::Artist;
    if (iequals_ascii(key, "charter")) return QueryField::Charter;
    if (iequals_ascii(key, "folder")) return QueryField::Folder;
    return std::nullopt;
}

// Reads the quoted run that opens at text[i] and moves i past its closing
// quote. A quote that is never closed runs to the end of the text.
std::string_view read_quoted(std::string_view text, size_t& i) {
    const size_t start = i + 1;
    const size_t close = text.find('"', start);
    const size_t end = close == npos ? text.size() : close;
    i = close == npos ? text.size() : close + 1;
    return text.substr(start, end - start);
}

void add_term(LibraryQuery& q, QueryField field, std::string_view raw, bool phrase) {
    std::string folded = fold_for_search(raw);
    if (!folded.empty() && folded.front() == ' ') folded.erase(0, 1);
    if (!folded.empty() && folded.back() == ' ') folded.pop_back();
    if (folded.empty()) return;
    q.terms.push_back(QueryTerm{field, std::move(folded), phrase});
}

void add_error(LibraryQuery& q, std::string message) {
    if (std::find(q.errors.begin(), q.errors.end(), message) == q.errors.end())
        q.errors.push_back(std::move(message));
}

void parse_stars(LibraryQuery& q, std::string_view value) {
    int n = -1;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [end, ec] = std::from_chars(first, last, n);
    if (value.empty() || ec != std::errc{} || end != last || n < 0 || n > kMaxStars) {
        add_error(q, kStarsError);
        return;
    }
    q.stars = n;
}

void parse_squeeze(LibraryQuery& q, std::string_view value) {
    double ms = 0.0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [end, ec] = std::from_chars(first, last, ms);
    if (value.empty() || ec != std::errc{} || end != last || !std::isfinite(ms)) {
        add_error(q, kSqueezeError);
        return;
    }
    q.squeeze_max_ms = ms;
}

// ---- matching --------------------------------------------------------------

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool term_matches(const QueryTerm& term, const SearchableRow& row) {
    switch (term.field) {
        case QueryField::Title: return contains(row.title, term.folded);
        case QueryField::Artist: return contains(row.artist, term.folded);
        case QueryField::Charter: return contains(row.charter, term.folded);
        case QueryField::Folder: return contains(row.folder, term.folded);
        case QueryField::Any:
            return contains(row.title, term.folded) || contains(row.artist, term.folded) ||
                   contains(row.charter, term.folded) || contains(row.folder, term.folded);
    }
    return false;
}

}  // namespace

std::string fold_for_search(std::string_view text) {
    std::string out;
    fold_into(text, out, nullptr);
    return out;
}

std::string strip_rich_tags(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            if (const size_t len = rich_tag_length(text, i)) {
                i += len;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

bool LibraryQuery::empty() const {
    return terms.empty() && !stars && !squeeze_max_ms;
}

LibraryQuery parse_library_query(std::string_view text) {
    LibraryQuery q;
    size_t i = 0;
    while (i < text.size()) {
        if (is_ascii_space(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            add_term(q, QueryField::Any, read_quoted(text, i), true);
            continue;
        }

        // A word runs to the next space or quote.
        size_t end = i;
        while (end < text.size() && text[end] != '"' &&
               !is_ascii_space(static_cast<unsigned char>(text[end])))
            ++end;
        const std::string_view word = text.substr(i, end - i);
        i = end;

        if (starts_with_ci(word, "squeeze<=")) {
            parse_squeeze(q, word.substr(9));
            continue;
        }
        if (starts_with_ci(word, "squeeze<")) {
            parse_squeeze(q, word.substr(8));
            continue;
        }

        const size_t colon = word.find(':');
        if (colon != npos) {
            const std::string_view key = word.substr(0, colon);
            const std::string_view value = word.substr(colon + 1);
            // field:"a phrase": the quote ended the word right after the colon.
            const bool quoted = value.empty() && i < text.size() && text[i] == '"';
            if (iequals_ascii(key, "stars")) {
                parse_stars(q, quoted ? read_quoted(text, i) : value);
                continue;
            }
            if (const std::optional<QueryField> field = field_named(key)) {
                add_term(q, *field, quoted ? read_quoted(text, i) : value, quoted);
                continue;
            }
        }
        add_term(q, QueryField::Any, word, false);
    }
    return q;
}

SearchableRow make_searchable(std::string_view title, std::string_view artist,
                              std::string_view charter, std::string_view folder) {
    SearchableRow row;
    row.title = fold_for_search(strip_rich_tags(title));
    row.artist = fold_for_search(strip_rich_tags(artist));
    row.charter = fold_for_search(strip_rich_tags(charter));
    row.folder = fold_for_search(strip_rich_tags(folder));
    return row;
}

bool query_matches(const LibraryQuery& q, const SearchableRow& row, const RowFacts& facts) {
    if (q.stars || q.squeeze_max_ms) {
        if (!facts.stars) return false;  // not analyzed: nothing to test
        if (q.stars && *facts.stars != *q.stars) return false;
        // A path with no squeeze passes any squeeze limit.
        if (q.squeeze_max_ms && facts.hardest_ms && *facts.hardest_ms > *q.squeeze_max_ms)
            return false;
    }
    for (const QueryTerm& term : q.terms)
        if (!term_matches(term, row)) return false;
    return true;
}

std::vector<MatchSpan> match_spans(const LibraryQuery& q, QueryField field,
                                   std::string_view display_text) {
    std::vector<MatchSpan> spans;
    if (q.terms.empty() || display_text.empty()) return spans;

    std::string folded;
    std::vector<SourceRange> source;
    fold_into(display_text, folded, &source);
    for (const QueryTerm& term : q.terms) {
        if (term.folded.empty()) continue;
        if (field != QueryField::Any && term.field != QueryField::Any && term.field != field)
            continue;
        for (size_t at = folded.find(term.folded); at != std::string::npos;
             at = folded.find(term.folded, at + 1))
            spans.push_back(
                MatchSpan{source[at].begin, source[at + term.folded.size() - 1].end});
    }

    std::sort(spans.begin(), spans.end(), [](const MatchSpan& a, const MatchSpan& b) {
        return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
    });
    std::vector<MatchSpan> merged;
    for (const MatchSpan& span : spans) {
        if (!merged.empty() && span.begin <= merged.back().end)
            merged.back().end = std::max(merged.back().end, span.end);
        else
            merged.push_back(span);
    }
    return merged;
}

}  // namespace hydra::app
