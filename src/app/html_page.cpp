#include "app/html_page.h"

#include <cstdint>
#include <cstdio>

namespace hydra::app::html {

std::string replace_all(std::string s, const std::string& from,
                        const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

void json_escape_into(std::string& out, const std::string& s) {
    char buf[16];
    out.push_back('"');
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
            ++i;
            continue;
        }

        // Decode one UTF-8 sequence to a code point. Malformed bytes fall
        // back to passing the single byte through as an escape, which the
        // parsers' valid UTF-8 output never exercises.
        uint32_t cp = 0;
        size_t len = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (c & 0x0Fu) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6 |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (c & 0x07u) << 18 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12 |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6 |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            len = 4;
        } else {
            cp = c;  // lone byte; emit as-is escaped
        }

        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            std::snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", 0xD800 + (v >> 10),
                          0xDC00 + (v & 0x3FF));
        } else {
            std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
        }
        out += buf;
        i += len;
    }
    out.push_back('"');
}

std::string render_page(const char* page_template, std::string data_json,
                        const std::string& subtitle, const std::string& footer) {
    data_json = replace_all(std::move(data_json), "</", "<\\/");

    std::string page = page_template;
    page = replace_all(std::move(page), "__SUBTITLE__", html_escape(subtitle));
    page = replace_all(std::move(page), "__FOOTER__", html_escape(footer));
    page = replace_all(std::move(page), "__DATA__", data_json);
    return page;
}

}  // namespace hydra::app::html
