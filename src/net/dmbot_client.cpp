#include "net/dmbot_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <stdexcept>

#include "json.hpp"

#include "core/strutil.h"  // to_lower_ascii
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

[[noreturn]] void fail(const std::string& what, DWORD error = GetLastError()) {
    throw std::runtime_error(what + " (error " + std::to_string(error) + ")");
}

bool cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }

// Why async: a synchronous WinHTTP call can't be interrupted. Microsoft forbids
// closing a handle while another thread is inside a synchronous call on it (a
// race that can crash; WinHTTP may reuse the handle value), and cancelling by
// closing the handle is documented only for asynchronous requests:
//   https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp
//   https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle
// So each step starts asynchronously and the calling thread waits for it,
// polling `cancel`. On cancel that same thread closes the request handle; no
// WinHTTP call of ours is in progress then, which is what the docs require.

// What the status callback reports back to the waiting thread. WinHTTP calls
// on its own threads; SetEvent/WaitForSingleObject order the field writes.
struct AsyncState {
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // a step finished
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // HANDLE_CLOSING arrived
    DWORD error = 0;  // nonzero when the step failed
    DWORD bytes = 0;  // DATA_AVAILABLE / READ_COMPLETE byte count
    AsyncState() = default;
    ~AsyncState() {
        if (done) CloseHandle(done);
        if (closed) CloseHandle(closed);
    }
    AsyncState(const AsyncState&) = delete;
    AsyncState& operator=(const AsyncState&) = delete;
};

void CALLBACK on_status(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info,
                        DWORD info_len) {
    auto* st = reinterpret_cast<AsyncState*>(context);
    if (!st) return;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        st->error = 0;
        SetEvent(st->done);
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        st->error = 0;
        st->bytes = *static_cast<const DWORD*>(info);
        SetEvent(st->done);
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        st->error = 0;
        st->bytes = info_len;
        SetEvent(st->done);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        st->error = static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError;
        SetEvent(st->done);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        SetEvent(st->closed);
        break;
    default:
        break;
    }
}

// The async request handle, closed in exactly one place. Closing cancels any
// pending step; once the callback is registered, the destructor then waits for
// HANDLE_CLOSING, the last callback WinHTTP makes for the handle, so no
// callback can touch the AsyncState (or a read buffer) after it is gone.
struct AsyncRequest {
    HINTERNET h = nullptr;
    AsyncState* st = nullptr;  // set once the callback is registered
    AsyncRequest() = default;
    ~AsyncRequest() {
        if (!h) return;
        WinHttpCloseHandle(h);
        if (st) WaitForSingleObject(st->closed, INFINITE);
    }
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;
};

// Wait for the step just started, checking `cancel` every 50 ms. On cancel,
// throw; unwinding runs ~AsyncRequest, which closes the handle and so cancels
// the step. A cancel that lands just as the step fails still reads "cancelled".
void await_step(AsyncState& st, const std::atomic<bool>* cancel, const char* what) {
    while (WaitForSingleObject(st.done, 50) == WAIT_TIMEOUT)
        if (cancelled(cancel)) throw std::runtime_error("cancelled");
    if (st.error) {
        if (cancelled(cancel)) throw std::runtime_error("cancelled");
        fail(what, st.error);
    }
}

// GET `url` and return the response body as UTF-8 bytes. Throws with a
// user-facing message on any transport error or a non-200 status, or
// "cancelled" within about 50 ms of `cancel` being set.
std::string http_get(const std::string& url, const std::atomic<bool>* cancel) {
    if (cancelled(cancel)) throw std::runtime_error("cancelled");

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
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                               WINHTTP_FLAG_ASYNC));
    if (!session) fail("could not start the network session");

    // Generous receive timeout: the render.com backend cold-starts after idle
    // and the first response can take tens of seconds. (resolve, connect, send,
    // receive) in ms.
    WinHttpSetTimeouts(session.h, 15000, 20000, 30000, 120000);

    Handle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!connect) fail("could not connect to the leaderboard");

    // Declared before `request` so both outlive it: its destructor waits out
    // the last callback, and a cancelled read may still be writing into `body`.
    AsyncState st;
    if (!st.done || !st.closed) fail("could not start the network session");
    std::string body;

    AsyncRequest request;
    request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h) fail("could not build the request");

    // Context first, callback second: once the callback is in, HANDLE_CLOSING
    // is guaranteed (it needs a non-null context), so the destructor may wait.
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&st);
    if (!WinHttpSetOption(request.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        fail("could not build the request");
    if (WinHttpSetStatusCallback(request.h, on_status,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                     WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        fail("could not build the request");
    request.st = &st;

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, context))
        fail("could not send the request");
    await_step(st, cancel, "could not send the request");

    if (cancelled(cancel)) throw std::runtime_error("cancelled");
    if (!WinHttpReceiveResponse(request.h, nullptr))
        fail("no response from the leaderboard");
    await_step(st, cancel, "no response from the leaderboard");

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX))
        fail("could not read the response status");
    if (status != 200)
        throw std::runtime_error("leaderboard returned HTTP " + std::to_string(status));

    // In async mode the byte counts arrive through the callback, so the out
    // parameters are null (as the docs require).
    for (;;) {
        if (cancelled(cancel)) throw std::runtime_error("cancelled");
        if (!WinHttpQueryDataAvailable(request.h, nullptr)) fail("could not read the response");
        await_step(st, cancel, "could not read the response");
        DWORD avail = st.bytes;
        if (avail == 0) break;
        size_t at = body.size();
        body.resize(at + avail);
        if (!WinHttpReadData(request.h, &body[at], avail, nullptr))
            fail("could not read the response");
        await_step(st, cancel, "could not read the response");
        body.resize(at + st.bytes);
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
    s.identifier = to_lower_ascii(jstr(entry, "identifier"));
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

std::vector<DmUser> parse_users_json(const std::string& body) {
    json root = parse_json(body);
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

std::vector<DmScore> parse_scores_json(const std::string& body) {
    json root = parse_json(body);

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

namespace {
Fetcher g_fetcher;

std::string get(const std::string& url, const std::atomic<bool>* cancel) {
    return g_fetcher ? g_fetcher(url, cancel) : http_get(url, cancel);
}
}  // namespace

void set_fetcher(Fetcher fetcher) { g_fetcher = std::move(fetcher); }

std::vector<DmUser> fetch_users(const std::string& api_base, const std::atomic<bool>* cancel) {
    return parse_users_json(get(api_base + "/all-users", cancel));
}

std::vector<DmScore> fetch_scores(const std::string& discord_id, const std::string& api_base,
                                  const std::atomic<bool>* cancel) {
    return parse_scores_json(get(api_base + "/user/" + discord_id + "/scores", cancel));
}

}  // namespace hydra::net
