#include "net/dmbot_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <stdexcept>

#include "json.hpp"

#include "core/version.h"
#include "core/winstr.h"

namespace hydra::net {

namespace {

using json = nlohmann::json;

// A WinHTTP HINTERNET that closes itself. WinHTTP has no null-handle constant of
// its own; nullptr is what every Open/Connect call returns on failure.
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what + " (error " + std::to_string(GetLastError()) + ")");
}

// GET `url` and return the response body as UTF-8 bytes. Throws with a
// user-facing message on any transport error or a non-200 status. Checks
// `cancel` between read chunks.
std::string http_get(const std::string& url, const std::atomic<bool>* cancel) {
    std::wstring wurl = hydra::utf8_to_wide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        fail("malformed leaderboard URL");

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    // A real User-Agent: Cloudflare (which fronts the backend) rejects empty
    // or missing ones with a 403 before the request ever reaches the API.
    Handle session(WinHttpOpen(L"Hydra/" HYDRA_VERSION_W,
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) fail("could not start the network session");

    // Generous receive timeout: the render.com backend cold-starts after idle
    // and the first response can take tens of seconds. (resolve, connect, send,
    // receive) in ms.
    WinHttpSetTimeouts(session.h, 15000, 20000, 30000, 120000);

    Handle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!connect) fail("could not connect to the leaderboard");

    Handle request(WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) fail("could not build the request");

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("could not send the request");
    if (!WinHttpReceiveResponse(request.h, nullptr))
        fail("no response from the leaderboard");

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX))
        fail("could not read the response status");
    if (status != 200)
        throw std::runtime_error("leaderboard returned HTTP " + std::to_string(status));

    std::string body;
    for (;;) {
        if (cancel && cancel->load()) throw std::runtime_error("cancelled");
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) fail("could not read the response");
        if (avail == 0) break;
        size_t at = body.size();
        body.resize(at + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, &body[at], avail, &read)) fail("could not read the response");
        body.resize(at + read);
    }
    return body;
}

// --- defensive JSON field readers (the API sends nulls for missing values) ---

std::string jstr(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    return it->is_string() ? it->get<std::string>() : it->dump();
}

int64_t jint(const json& j, const char* key, int64_t def = 0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null() || !it->is_number()) return def;
    return it->get<int64_t>();
}

std::optional<int> joptint(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null() || !it->is_number()) return std::nullopt;
    return it->get<int>();
}

std::string lower_hex(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string join_charters(const json& entry) {
    auto it = entry.find("charter_refs");
    if (it == entry.end() || !it->is_array()) return {};
    std::string out;
    for (const json& c : *it) {
        if (!c.is_string()) continue;
        if (!out.empty()) out += ", ";
        out += c.get<std::string>();
    }
    return out;
}

DmScore parse_score(const json& entry, bool known) {
    DmScore s;
    s.identifier = lower_hex(jstr(entry, "identifier"));
    s.song_name = jstr(entry, "song_name");
    s.artist = jstr(entry, "artist");
    s.charter = join_charters(entry);
    s.score = jint(entry, "score");
    s.is_fc = jint(entry, "is_fc") != 0;
    s.percent = (int)jint(entry, "percent");
    s.speed = (int)jint(entry, "speed", 100);
    s.rank = joptint(entry, "rank");
    s.posted = jstr(entry, "posted");
    s.known = known;
    return s;
}

json parse_json(const std::string& body) {
    try {
        return json::parse(body);
    } catch (const std::exception&) {
        throw std::runtime_error("the leaderboard sent a response Hydra couldn't read");
    }
}

}  // namespace

std::vector<DmUser> fetch_users(const std::string& api_base, const std::atomic<bool>* cancel) {
    json root = parse_json(http_get(api_base + "/all-users", cancel));
    if (!root.is_array()) throw std::runtime_error("unexpected user-list format");

    std::vector<DmUser> users;
    users.reserve(root.size());
    for (const json& u : root) {
        DmUser du;
        du.id = jstr(u, "id");
        du.username = jstr(u, "username");
        du.elo = joptint(u, "elo");
        auto stats = u.find("stats");
        if (stats != u.end() && stats->is_object()) {
            du.total_scores = (int)jint(*stats, "total_scores");
            du.total_score = jint(*stats, "total_score");
        }
        if (!du.id.empty()) users.push_back(std::move(du));
    }
    return users;
}

std::vector<DmScore> fetch_scores(const std::string& discord_id, const std::string& api_base,
                                  const std::atomic<bool>* cancel) {
    json root = parse_json(http_get(api_base + "/user/" + discord_id + "/scores", cancel));

    std::vector<DmScore> scores;
    auto add_all = [&](const char* key, bool known) {
        auto arr = root.find(key);
        if (arr == root.end() || !arr->is_array()) return;
        scores.reserve(scores.size() + arr->size());
        for (const json& entry : *arr) {
            DmScore s = parse_score(entry, known);
            if (!s.identifier.empty()) scores.push_back(std::move(s));
        }
    };
    add_all("scores", /*known=*/true);
    add_all("unknown_scores", /*known=*/false);
    return scores;
}

}  // namespace hydra::net
