// Shared plumbing for the three HTML report pages (app/report.cpp,
// app/dm_report.cpp, app/fill_report.cpp): the one stylesheet and script they
// all use, template substitution, and the escaping helpers they embed their
// row data with.

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

// ---- the shared report page -----------------------------------------------
// All three report pages are one stylesheet and one script wrapped around each
// page's own title, body markup and PAGE settings (docs/adr/0016). Both stay
// ASCII: this file compiles into hydra_core, so a glyph goes in as an HTML
// entity or a \uXXXX JavaScript escape.
extern const char* const kReportCss;     // every rule the three pages use
extern const char* const kReportJsHead;  // the data tag, DATA, DASH, fmt, fmtMs
extern const char* const kReportJs;      // sorting, filtering, drawing, first render

// One page's template: a whole standards-mode document (doctype, <html
// lang="en">, head, body) with the shared stylesheet and script around the
// page's <title> text, its body markup and its `const PAGE = {...};` script.
// A column in PAGE.cols may carry `d:'...'`, its definition: the header's
// hover text and the footer legend (#legend, when the body has one) show it.
// The result still carries __SUBTITLE__, __FOOTER__ and __DATA__ for
// render_page to fill.
std::string page_template(const char* title, const char* body, const char* page_js);

}  // namespace hydra::app::html

#endif  // HYDRA_APP_HTML_PAGE_H
