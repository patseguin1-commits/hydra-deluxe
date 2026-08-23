// Shared plumbing for the HTML report pages (app/report.cpp and
// app/dm_report.cpp): template substitution and the escaping helpers both
// pages embed their row data with.

#ifndef HYDRA_APP_HTML_PAGE_H
#define HYDRA_APP_HTML_PAGE_H

#include <string>

namespace hydra::app::html {

// str.replace(old, new) for the one-shot template placeholders.
std::string replace_all(std::string s, const std::string& from,
                        const std::string& to);

// html.escape(s, quote=True): & first, then the rest.
std::string html_escape(const std::string& s);

// json.dumps string escaping with the default ensure_ascii=True: every
// non-ASCII code point becomes \uXXXX (a surrogate pair beyond the BMP),
// control characters get their short escapes, and everything else passes
// through. Input is UTF-8. Appends the quoted string to `out`.
void json_escape_into(std::string& out, const std::string& s);

// Fill a page template: guard the embedded JSON against `</` closing the
// script tag, then substitute __SUBTITLE__, __FOOTER__, and __DATA__.
std::string render_page(const char* page_template, std::string data_json,
                        const std::string& subtitle, const std::string& footer);

// ---- sortable page fragments ----------------------------------------------
// The shared skeleton of the two sortable report pages (app/report.cpp and
// app/dm_report.cpp): theme + chrome CSS, the table CSS, the chip base, the
// sort machinery, and the deferred first render. Each page concatenates these
// with its own title, column widths, chip colors, body, and script. The path
// report's assembled bytes stay pinned to hydra_report.py's PAGE, so the
// fragments are canon from that page verbatim — a change here changes both
// pages, which is the point.
extern const char* const kSortableHead;      // the two <meta> lines
extern const char* const kSortableCssCore;   // :root themes .. .count
extern const char* const kSortableCssTable;  // .tablewrap .. sticky first col
extern const char* const kSortableCssChip;   // .chip base rule
extern const char* const kSortableCssTail;   // .empty/footer + </style>
extern const char* const kSortableJsSorter;  // setSort + header builder + fmt
extern const char* const kSortableJsBoot;    // deferred first render + </script>

}  // namespace hydra::app::html

#endif  // HYDRA_APP_HTML_PAGE_H
