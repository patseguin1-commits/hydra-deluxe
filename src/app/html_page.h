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

}  // namespace hydra::app::html

#endif  // HYDRA_APP_HTML_PAGE_H
