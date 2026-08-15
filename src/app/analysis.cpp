#include "app/analysis.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

#include "search/pather.h"

namespace hydra::app {

namespace {

// ---- filesystem helpers (Windows API, UTF-8 std::string <-> wide) --------
//
// Mirrors the encoding approach parse/song.cpp's read_file_bytes uses: chart
// libraries routinely contain non-ASCII folder names, so every path that
// touches the filesystem API is round-tripped through UTF-16.

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                   nullptr, 0);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], wlen);
    return w;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], len,
                        nullptr, nullptr);
    return s;
}

std::vector<uint8_t> read_file_bytes(const std::string& utf8_path) {
    FILE* f = _wfopen(utf8_to_wide(utf8_path).c_str(), L"rb");
    if (f == nullptr) throw std::runtime_error("cannot open file: " + utf8_path);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
    std::fclose(f);
    return buf;
}

struct DirEntry {
    std::string name;
    bool is_dir;
};

std::vector<DirEntry> list_dir(const std::string& dir_utf8) {
    std::vector<DirEntry> out;
    std::wstring pattern = utf8_to_wide(dir_utf8) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        out.push_back({wide_to_utf8(name),
                       (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool is_dir(const std::string& path_utf8) {
    DWORD attrs = GetFileAttributesW(utf8_to_wide(path_utf8).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    return (last == '\\' || last == '/') ? a + b : a + "\\" + b;
}

std::string parent_of(const std::string& path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? std::string() : p.substr(0, pos);
}

// os.path.relpath(target, base), for the folders this walk already knows are
// nested under `base` (or equal to it). Falls back to the raw target for any
// path that isn't, which discover_charts's own walk never produces.
std::string relpath(const std::string& target, const std::string& base) {
    std::string t = target, b = base;
    while (!t.empty() && (t.back() == '\\' || t.back() == '/')) t.pop_back();
    while (!b.empty() && (b.back() == '\\' || b.back() == '/')) b.pop_back();
    if (t == b) return ".";
    if (t.size() > b.size() && t.compare(0, b.size(), b) == 0 &&
        (t[b.size()] == '\\' || t[b.size()] == '/'))
        return t.substr(b.size() + 1);
    return t;
}

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    std::string suf = lower(suffix);
    std::string low = lower(s);
    return low.size() >= suf.size() &&
           low.compare(low.size() - suf.size(), suf.size(), suf) == 0;
}

// ---- MD5 (Windows CNG), mirroring hashlib.file_digest(f, "md5") ----------

std::string md5_hex(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0)))
        throw std::runtime_error("BCryptOpenAlgorithmProvider(MD5) failed");

    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0);

        UCHAR digest[16];
        if (BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static const char* kHexDigits = "0123456789abcdef";
            hex.resize(32);
            for (int i = 0; i < 16; ++i) {
                hex[static_cast<size_t>(2 * i)] = kHexDigits[digest[i] >> 4];
                hex[static_cast<size_t>(2 * i + 1)] = kHexDigits[digest[i] & 0xF];
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);

    if (hex.empty()) throw std::runtime_error("MD5 hashing failed");
    return hex;
}

// ---- song.ini metadata, mirroring ScanItem.get_metadata_ini --------------
//
// A minimal INI reader: a [Song]/[song] section, `key = value` lines,
// `;`/`#` comments. Chart libraries are UTF-8 in practice; a leading BOM is
// stripped and anything else is read byte-for-byte rather than replicating
// Python's utf-8/utf-8-sig/ansi fallback chain.

std::tuple<std::string, std::string, std::string> read_metadata_ini(const std::string& path) {
    std::vector<uint8_t> raw = read_file_bytes(path);
    size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) start = 3;
    std::string text(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start);

    std::string title = "<unknown title>";
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";

    bool in_song_section = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos
                                                                      : eol - pos);
        pos = (eol == std::string::npos) ? text.size() + 1 : eol + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        line = line.substr(a);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            std::string section = lower(line.substr(1, line.size() - 2));
            in_song_section = (section == "song");
            continue;
        }
        if (!in_song_section) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(line.substr(0, eq));
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string value = line.substr(eq + 1);
        size_t vb = value.find_first_not_of(" \t");
        value = (vb == std::string::npos) ? std::string() : value.substr(vb);

        if (key == "name") title = value;
        else if (key == "artist") artist = value;
        // Only `charter` — Python's get_metadata_ini never reads the `frets`
        // alias, and some inis carry both with different values.
        else if (key == "charter") charter = value;
    }

    return {title, artist, charter};
}

// ---- .sng metadata, mirroring ScanItem.get_metadata_sng -------------------

std::tuple<std::string, std::string, std::string> read_metadata_sng(const std::string& path) {
    std::vector<uint8_t> buf = read_file_bytes(path);

    auto u64_at = [&buf](size_t pos) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(buf[pos + i]) << (8 * i);
        return v;
    };
    auto u32_at = [&buf](size_t pos) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf[pos + i]) << (8 * i);
        return v;
    };

    std::string title = "<unknown title>";
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";

    const size_t kMetadataCountOffset = 34;
    if (buf.size() < kMetadataCountOffset + 8) return {title, artist, charter};

    size_t pos = kMetadataCountOffset;
    uint64_t count = u64_at(pos);
    pos += 8;

    for (uint64_t i = 0; i < count; ++i) {
        if (pos + 4 > buf.size()) break;
        uint32_t key_len = u32_at(pos);
        pos += 4;
        if (pos + key_len > buf.size()) break;
        std::string key = lower(std::string(reinterpret_cast<const char*>(&buf[pos]), key_len));
        pos += key_len;

        if (pos + 4 > buf.size()) break;
        uint32_t value_len = u32_at(pos);
        pos += 4;
        if (pos + value_len > buf.size()) break;
        std::string value(reinterpret_cast<const char*>(&buf[pos]), value_len);
        pos += value_len;

        if (key == "name") title = value;
        else if (key == "artist") artist = value;
        else if (key == "charter") charter = value;
    }

    return {title, artist, charter};
}

}  // namespace

// ---- ScanItem ---------------------------------------------------------

ScanItem ScanItem::from_notes_ini_pair(const std::string& notes_path,
                                       const std::string& ini_path,
                                       const std::string& rootfolder) {
    ScanItem item;
    item.md5 = md5_hex(read_file_bytes(notes_path));
    std::tie(item.title, item.artist, item.charter) = read_metadata_ini(ini_path);
    item.notespath = notes_path;
    item.rootfolder = rootfolder;
    return item;
}

ScanItem ScanItem::from_sng(const std::string& sng_path, const std::string& rootfolder) {
    ScanItem item;
    item.md5 = md5_hex(read_file_bytes(sng_path));
    std::tie(item.title, item.artist, item.charter) = read_metadata_sng(sng_path);
    item.notespath = sng_path;
    item.rootfolder = rootfolder;
    return item;
}

// ---- discovery ----------------------------------------------------------

namespace {

void process_folder(const std::string& folder, const std::string& origin_folder,
                    std::vector<ScanItem>& out) {
    std::string found_mid, found_chart, found_ini;
    std::vector<std::string> found_sngs;

    for (const DirEntry& e : list_dir(folder)) {
        if (e.is_dir) continue;
        std::string full = join_path(folder, e.name);
        if (e.name == "notes.mid") found_mid = full;
        else if (e.name == "notes.chart") found_chart = full;
        else if (e.name == "song.ini") found_ini = full;
        else if (ends_with_ci(e.name, ".sng")) found_sngs.push_back(full);
    }

    if (!found_mid.empty() && !found_ini.empty())
        out.push_back(ScanItem::from_notes_ini_pair(found_mid, found_ini, origin_folder));
    else if (!found_chart.empty() && !found_ini.empty())
        out.push_back(ScanItem::from_notes_ini_pair(found_chart, found_ini, origin_folder));

    for (const std::string& f : found_sngs)
        out.push_back(ScanItem::from_sng(f, origin_folder));
}

}  // namespace

std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders,
    const std::function<void(int)>& cb_progress) {
    std::vector<ScanItem> scanitems;
    std::vector<std::string> errors;

    std::vector<std::pair<std::string, std::string>> unexplored;
    std::set<std::string> visited;
    for (const std::string& root : rootfolders) {
        if (is_dir(root)) unexplored.push_back({root, root});
        visited.insert(root);
    }

    while (!unexplored.empty()) {
        auto [dir, origin] = unexplored.back();
        unexplored.pop_back();

        try {
            process_folder(dir, relpath(parent_of(dir), origin), scanitems);
            for (const DirEntry& e : list_dir(dir)) {
                if (!e.is_dir) continue;
                std::string subpath = join_path(dir, e.name);
                if (visited.insert(subpath).second) {
                    if (cb_progress) cb_progress(static_cast<int>(visited.size()));
                    unexplored.push_back({subpath, origin});
                }
            }
        } catch (const std::exception& e) {
            errors.push_back(e.what());
        }
    }

    return {scanitems, errors};
}

int get_folder_count(const std::vector<std::string>& rootfolders,
                     const std::function<void(int)>& cb_progress) {
    std::vector<std::pair<std::string, std::string>> unexplored;
    std::set<std::string> visited;
    for (const std::string& root : rootfolders) {
        if (is_dir(root)) unexplored.push_back({root, root});
        visited.insert(root);
    }

    while (!unexplored.empty()) {
        auto [dir, origin] = unexplored.back();
        unexplored.pop_back();
        for (const DirEntry& e : list_dir(dir)) {
            if (!e.is_dir) continue;
            std::string subpath = join_path(dir, e.name);
            if (visited.insert(subpath).second) {
                if (cb_progress) cb_progress(static_cast<int>(visited.size()));
                unexplored.push_back({subpath, origin});
            }
        }
    }
    return static_cast<int>(visited.size());
}

// ---- chord counting -------------------------------------------------------

std::map<std::string, int> count_chart_chords(const std::string& filepath) {
    Song song = load_songpath(filepath, "Expert", true, true);
    std::map<std::string, int> counts;
    for (const SongTimestamp& ts : song.sequence) ++counts[ts.chord.code()];
    return counts;
}

// ---- analysis -------------------------------------------------------------

AnalysisResult analyze_chart_file(const std::string& filepath,
                                  const AnalysisSettings& settings,
                                  const std::function<void(float)>& on_progress) {
    Song song =
        load_songpath(filepath, settings.difficulty, settings.prodrums, settings.bass2x);
    HydraRecord record = analyze_chart(song, /*capped=*/!settings.uncapped,
                                       settings.depth_mode, settings.depth_value,
                                       settings.ms_filter, settings.sp_cap, on_progress,
                                       settings.uncapped_time_budget_s);
    return AnalysisResult{std::move(record), std::move(song)};
}

// ---- batch runner -----------------------------------------------------

constexpr int kBatchMaxWorkers = 8;

int batch_worker_count() {
    unsigned int hw = std::thread::hardware_concurrency();
    int guess = hw > 0 ? static_cast<int>(hw) - 1 : 1;
    return std::max(1, std::min(guess, kBatchMaxWorkers));
}

namespace {

struct WorkResult {
    ScanItem item;
    std::optional<store::PreparedRow> row;
    std::optional<AnalysisResult> analysis;
    std::string error;
};

}  // namespace

void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
              const AnalysisSettings& settings, store::RecordStore& store, bool redo,
              int worker_count,
              const std::function<void(const BatchProgress&)>& on_progress,
              const std::function<void(const std::string&, const std::string&)>& on_error,
              const std::function<void(const ScanItem&, const store::PreparedRow&)>& on_result,
              const std::atomic<bool>* cancel) {
    std::vector<const ScanItem*> todo;
    for (const ScanItem& item : items) {
        if (!redo && store.has_record(item.md5, chartmode)) continue;
        todo.push_back(&item);
    }

    BatchProgress progress;
    progress.total = static_cast<int>(todo.size());
    if (on_progress) on_progress(progress);
    if (todo.empty()) return;

    std::mutex result_mu;
    std::condition_variable result_cv;
    std::deque<WorkResult> results;
    std::atomic<size_t> next{0};

    int nworkers = std::max(1, worker_count);
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(nworkers));
    for (int w = 0; w < nworkers; ++w) {
        pool.emplace_back([&]() {
            for (;;) {
                size_t i = next.fetch_add(1);
                if (i >= todo.size()) break;
                if (cancel && cancel->load()) break;

                const ScanItem* item = todo[i];
                WorkResult wr;
                wr.item = *item;
                try {
                    AnalysisResult ar = analyze_chart_file(item->notespath, settings);
                    wr.row = store::prepare_row(item->md5, chartmode, ar.record,
                                                settings.uncapped);
                    wr.analysis = std::move(ar);
                } catch (const std::exception& e) {
                    wr.error = e.what();
                }

                {
                    std::lock_guard<std::mutex> lock(result_mu);
                    results.push_back(std::move(wr));
                }
                result_cv.notify_one();
            }
        });
    }

    int completed = 0;
    while (completed < static_cast<int>(todo.size())) {
        WorkResult wr;
        {
            std::unique_lock<std::mutex> lock(result_mu);
            result_cv.wait(lock, [&] { return !results.empty(); });
            wr = std::move(results.front());
            results.pop_front();
        }

        ++completed;
        if (!wr.error.empty()) {
            if (on_error) on_error(wr.item.title, wr.error);
        } else {
            store.add_song(wr.item.md5, wr.item.title, wr.item.artist, wr.item.charter,
                           wr.analysis->song);
            store.add_row(*wr.row);
            if (on_result) on_result(wr.item, *wr.row);
        }

        progress.completed = completed;
        progress.current_title = wr.item.title;
        if (on_progress) on_progress(progress);

        if (cancel && cancel->load()) break;
    }

    for (std::thread& t : pool) t.join();
}

}  // namespace hydra::app
