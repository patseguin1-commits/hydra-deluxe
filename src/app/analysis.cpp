#include "app/analysis.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_set>

#include "app/dynamics_breakdown.h"
#include "app/work_pool.h"
#include "core/winstr.h"
#include "parse/srb.h"
#include "parse/chart_files.h"
#include "parse/sng.h"
#include "search/pather.h"

namespace hydra::app {

namespace {

struct DirEntry {
    std::string name;
    bool is_dir;
    // Size and last-write time (FILETIME ticks) straight from the find data —
    // the rescan cache's change fingerprint, at no extra stat cost.
    uint64_t size = 0;
    uint64_t mtime = 0;
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
        uint64_t size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        uint64_t mtime = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                         fd.ftLastWriteTime.dwLowDateTime;
        out.push_back({wide_to_utf8(name),
                       (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, size, mtime});
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

// Case-insensitive suffix check without the per-call allocations the old
// lowercase-both-strings version paid on every file in every folder.
bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = static_cast<unsigned char>(s[s.size() - n + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

// ---- MD5 (Windows CNG), mirroring hashlib.file_digest(f, "md5") ----------
//
// The digest of the full raw chart file is record identity (songmeta.hyhash),
// so it must stay exactly MD5-of-all-bytes; only *how* the bytes reach the
// hash changed: streamed in chunks (like Python's file_digest) instead of a
// whole-file buffer, with the algorithm provider opened once per scan worker
// instead of once per file.

class Md5Provider {
public:
    Md5Provider() {
        if (!BCRYPT_SUCCESS(
                BCryptOpenAlgorithmProvider(&alg_, BCRYPT_MD5_ALGORITHM, nullptr, 0)))
            throw std::runtime_error("BCryptOpenAlgorithmProvider(MD5) failed");
    }
    ~Md5Provider() {
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    Md5Provider(const Md5Provider&) = delete;
    Md5Provider& operator=(const Md5Provider&) = delete;

    BCRYPT_ALG_HANDLE handle() const { return alg_; }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
};

struct HashedFile {
    std::string md5;
    std::vector<uint8_t> head;  // first `head_capture` bytes, for .sng metadata
};

HashedFile stream_md5(BCRYPT_ALG_HANDLE alg, const std::string& path,
                      size_t head_capture) {
    FILE* f = fopen_utf8(path, L"rb");
    if (f == nullptr) throw std::runtime_error("cannot open file: " + path);

    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::fclose(f);
        throw std::runtime_error("MD5 hashing failed");
    }

    HashedFile out;
    std::vector<uint8_t> buf(1 << 20);
    size_t got;
    while ((got = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        BCryptHashData(hash, buf.data(), static_cast<ULONG>(got), 0);
        if (out.head.size() < head_capture) {
            size_t want = std::min(head_capture - out.head.size(), got);
            out.head.insert(out.head.end(), buf.data(), buf.data() + want);
        }
    }
    std::fclose(f);

    UCHAR digest[16];
    bool ok = BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    BCryptDestroyHash(hash);
    if (!ok) throw std::runtime_error("MD5 hashing failed");

    static const char* kHexDigits = "0123456789abcdef";
    out.md5.resize(32);
    for (int i = 0; i < 16; ++i) {
        out.md5[static_cast<size_t>(2 * i)] = kHexDigits[digest[i] >> 4];
        out.md5[static_cast<size_t>(2 * i + 1)] = kHexDigits[digest[i] & 0xF];
    }
    return out;
}

// ---- song.ini metadata, mirroring ScanItem.get_metadata_ini --------------
//
// The name, artist and charter keys, read through read_song_ini_keys (the
// one song.ini reader, shared with the Preview's delay). Its declaration in
// analysis.h says which lines count.

std::tuple<std::string, std::string, std::string> read_metadata_ini(const std::string& path) {
    const std::map<std::string, std::string> ini = read_song_ini_keys(path);

    // Empty = no usable name; discover_charts applies the one fallback.
    std::string title;
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";

    if (auto it = ini.find("name"); it != ini.end()) title = it->second;
    if (auto it = ini.find("artist"); it != ini.end()) artist = it->second;
    // Only `charter` — Python's get_metadata_ini never reads the `frets`
    // alias, and some inis carry both with different values.
    if (auto it = ini.find("charter"); it != ini.end()) charter = it->second;
    return {title, artist, charter};
}

// ---- .sng metadata, mirroring ScanItem.get_metadata_sng -------------------
//
// Parses the metadata block from the head bytes captured while the file was
// being hashed — the old version read the entire archive (chart + audio, can
// be hundreds of MB) a second time to get three strings from its first few
// KB. A truncated buffer degrades exactly like a truncated file did: the
// bounds checks stop early and a missing name stays empty and the other keys keep their <unknown> defaults.

// How much of a .sng/.srb to keep for metadata. A .sng block starts at offset
// 34 and a .srb's deflated block at offset 16; real metadata is a few KB, so
// 1 MB is far beyond any legitimate block.
constexpr size_t kSngHeadCapture = 1 << 20;

std::tuple<std::string, std::string, std::string> parse_sng_metadata(
    const std::vector<uint8_t>& buf) {
    // Empty = no usable name; discover_charts applies the one fallback.
    std::string title;
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";

    for (const auto& [raw_key, value] : sng_read_metadata(buf)) {
        const std::string key = lower(raw_key);
        if (key == "name") title = value;
        else if (key == "artist") artist = value;
        else if (key == "charter") charter = value;
    }

    return {title, artist, charter};
}

// ---- .srb metadata --------------------------------------------------------
//
// Clone Hero's bundled songs (see parse/srb.h for the reverse-engineered
// container layout). The metadata block is a deflate stream starting right
// after the 16-byte header, so the head bytes captured while hashing always
// contain it. Any parse failure degrades to the <unknown> defaults, matching
// the .sng path.

std::tuple<std::string, std::string, std::string> parse_srb_metadata(
    const std::vector<uint8_t>& buf) {
    // Empty = no usable name; discover_charts applies the one fallback.
    std::string title;
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";

    try {
        std::vector<uint8_t> meta = srb_inflate_stream(
            buf.data(), buf.size(), kSrbHeaderSize, kSrbMaxMetadata, nullptr);
        SrbMetadata md;
        if (srb_parse_metadata(meta, md)) {
            if (!md.name.empty()) title = md.name;
            if (!md.artist.empty()) artist = md.artist;
            if (!md.charter.empty()) charter = md.charter;
        }
    } catch (const std::exception&) {
        // Corrupt/truncated container: keep the defaults.
    }

    return {title, artist, charter};
}

// ---- discovery ----------------------------------------------------------
//
// Two stages. Enumerate: a serial single-pass walk (one directory listing
// per folder, no file contents touched) collecting every chart-bearing
// folder's pending work. Read: a batch_worker_count() thread pool hashes the
// chart files and reads their metadata, short-circuiting through the rescan
// cache when a file's size+mtime fingerprint is unchanged. Results keep the
// walk's order, so output ordering matches the old serial scanner.

// One chart the walk found, before any of its bytes have been read. Folder
// charts are a notes.mid/.chart plus a song.ini; .sng and .srb are standalone
// archives with embedded metadata.
enum class ChartKind { Folder, Sng, Srb };

struct PendingChart {
    ChartKind kind = ChartKind::Folder;
    std::string notes_path;  // the hashed file: notes.mid/.chart, .sng, or .srb
    std::string ini_path;    // empty for archives
    std::string rootfolder;
    std::string sig;
};

std::string sig_of(const DirEntry& notes, const DirEntry* ini) {
    std::string sig = std::to_string(notes.size) + ":" + std::to_string(notes.mtime);
    if (ini) sig += ":" + std::to_string(ini->size) + ":" + std::to_string(ini->mtime);
    return sig;
}

}  // namespace

// Chart libraries are UTF-8 in practice; a leading BOM is stripped and
// anything else is read byte-for-byte rather than replicating Python's
// utf-8/utf-8-sig/ansi fallback chain.
std::map<std::string, std::string> read_song_ini_keys(const std::string& path) {
    std::vector<uint8_t> raw = read_file_bytes(path);
    size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) start = 3;
    std::string text(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start);

    std::map<std::string, std::string> keys;
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
        keys[key] = value;
    }
    return keys;
}

// Hashes the whole chart file with MD5, the same way the library scan does.
// So the result here always matches the hyhash already stored in the
// songmeta/charts rows for that chart.
std::string hash_chart_file(const std::string& path) {
    try {
        Md5Provider md5;
        HashedFile hf = stream_md5(md5.handle(), path, 0);
        return hf.md5;
    } catch (const std::exception&) {
        return {};
    }
}

std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders, const ScanCallbacks& callbacks,
    const store::ChartLibraryCache* cache) {
    std::vector<std::string> errors;
    const std::atomic<bool>* cancel = callbacks.cancel;

    // ---- stage 1: enumerate ------------------------------------------------
    std::vector<PendingChart> pending;
    std::vector<std::pair<std::string, std::string>> unexplored;
    std::set<std::string> visited;
    for (const std::string& root : rootfolders) {
        if (is_dir(root)) unexplored.push_back({root, root});
        visited.insert(root);
    }

    while (!unexplored.empty()) {
        if (cancel && cancel->load()) break;
        auto [dir, origin] = unexplored.back();
        unexplored.pop_back();

        try {
            std::vector<DirEntry> entries = list_dir(dir);

            const DirEntry* found_mid = nullptr;
            const DirEntry* found_chart = nullptr;
            const DirEntry* found_ini = nullptr;
            std::vector<std::pair<const DirEntry*, ChartKind>> found_archives;
            std::vector<const DirEntry*> subdirs;
            for (const DirEntry& e : entries) {
                if (e.is_dir) subdirs.push_back(&e);
                else if (notes_file_format(e.name) == ChartFormat::Mid) found_mid = &e;
                else if (notes_file_format(e.name) == ChartFormat::Chart) found_chart = &e;
                else if (is_song_ini(e.name)) found_ini = &e;
                else if (chart_format_of(e.name) == ChartFormat::Sng)
                    found_archives.push_back({&e, ChartKind::Sng});
                else if (chart_format_of(e.name) == ChartFormat::Srb)
                    found_archives.push_back({&e, ChartKind::Srb});
            }

            std::string rootfolder = relpath(parent_of(dir), origin);
            const DirEntry* notes = found_mid ? found_mid : found_chart;
            if (notes && found_ini) {
                PendingChart pc;
                pc.notes_path = join_path(dir, notes->name);
                pc.ini_path = join_path(dir, found_ini->name);
                pc.rootfolder = rootfolder;
                pc.sig = sig_of(*notes, found_ini);
                pending.push_back(std::move(pc));
            }
            for (auto [archive, kind] : found_archives) {
                PendingChart pc;
                pc.kind = kind;
                pc.notes_path = join_path(dir, archive->name);
                pc.rootfolder = rootfolder;
                pc.sig = sig_of(*archive, nullptr);
                pending.push_back(std::move(pc));
            }

            for (const DirEntry* sub : subdirs) {
                std::string subpath = join_path(dir, sub->name);
                if (visited.insert(subpath).second) {
                    if (callbacks.on_folders)
                        callbacks.on_folders(static_cast<int>(visited.size()));
                    unexplored.push_back({subpath, origin});
                }
            }
        } catch (const std::exception& e) {
            errors.push_back(e.what());
        }
    }

    // ---- stage 2: read (hash + metadata), parallel -------------------------
    int total = static_cast<int>(pending.size());
    if (callbacks.on_charts) callbacks.on_charts(0, total, 0);

    std::vector<std::optional<ScanItem>> results(pending.size());
    if (total > 0 && !(cancel && cancel->load())) {
        struct ReadNote {
            bool cached = false;
            std::string error;
        };
        int done = 0, cached_count = 0;
        run_work_pool<ReadNote>(
            pending.size(), batch_worker_count(), cancel,
            [&](size_t i) {
                // One CNG provider per worker thread, reused across every file
                // it hashes and closed when the worker exits. Created lazily
                // so an all-cache-hits rescan never touches CNG at all.
                thread_local std::optional<Md5Provider> md5;

                const PendingChart& pc = pending[i];
                ReadNote note;
                try {
                    if (cache) {
                        auto it = cache->find(pc.notes_path);
                        if (it != cache->end() && it->second.sig == pc.sig) {
                            results[i] = ScanItem{it->second.md5, it->second.title,
                                                  it->second.artist, it->second.charter,
                                                  pc.notes_path, pc.rootfolder, pc.sig};
                            note.cached = true;
                        }
                    }
                    if (!results[i]) {
                        if (!md5) md5.emplace();
                        ScanItem item;
                        if (pc.kind != ChartKind::Folder) {
                            HashedFile hf =
                                stream_md5(md5->handle(), pc.notes_path, kSngHeadCapture);
                            item.md5 = std::move(hf.md5);
                            std::tie(item.title, item.artist, item.charter) =
                                pc.kind == ChartKind::Sng
                                    ? parse_sng_metadata(hf.head)
                                    : parse_srb_metadata(hf.head);
                        } else {
                            HashedFile hf = stream_md5(md5->handle(), pc.notes_path, 0);
                            item.md5 = std::move(hf.md5);
                            std::tie(item.title, item.artist, item.charter) =
                                read_metadata_ini(pc.ini_path);
                        }
                        item.notespath = pc.notes_path;
                        item.rootfolder = pc.rootfolder;
                        item.sig = pc.sig;
                        results[i] = std::move(item);
                    }
                } catch (const std::exception& e) {
                    note.error = e.what();
                }
                return note;
            },
            // Progress and error callbacks fire here, on the calling thread
            // only, as they do for run_batch.
            [&](ReadNote&& note) {
                ++done;
                if (note.cached) ++cached_count;
                if (!note.error.empty()) errors.push_back(std::move(note.error));
                if (callbacks.on_charts) callbacks.on_charts(done, total, cached_count);
            });
    }

    std::vector<ScanItem> scanitems;
    scanitems.reserve(results.size());
    for (std::optional<ScanItem>& r : results) {
        if (!r) continue;
        // The one fallback, whichever source produced the title: a fresh
        // song.ini, .sng or .srb read, or the rescan cache holding an older
        // scan's blank or "<unknown title>".
        r->title = title_or_unknown(std::move(r->title));
        scanitems.push_back(std::move(*r));
    }
    return {scanitems, errors};
}

std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders,
    const std::function<void(int)>& cb_progress) {
    ScanCallbacks callbacks;
    callbacks.on_folders = cb_progress;
    return discover_charts(rootfolders, callbacks, nullptr);
}

// ---- analysis -------------------------------------------------------------

AnalysisResult analyze_chart_file(const std::string& filepath,
                                  const AnalysisSettings& settings,
                                  const std::function<void(float)>& on_progress) {
    Song song = load_songpath(filepath, settings.prodrums, settings.bass2x,
                              settings.difficulty, settings.rules);
    // Say which difficulty is missing. The search's own backstop can only say
    // "no notes"; here we know what the user asked for, and a chart that
    // simply has no Hard charting is the common case.
    if (song.is_empty())
        throw ChartFileError(no_notes_message(settings.difficulty, settings.prodrums));
    HydraRecord record = analyze_chart(song, settings, on_progress);
    return AnalysisResult{std::move(record), std::move(song)};
}

// ---- batch runner -----------------------------------------------------

// 8 = a memory/throughput choice: enough threads to keep a modern CPU busy
// while capping peak memory (a discography chart can reach hundreds of MB per
// worker). Kept fixed; batch_worker_count in analysis.h leaves one core free.
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
    // Counted on the worker, so the consumer only writes.
    std::optional<store::DynamicsEntry> dynamics;
    std::string error;
    // The search stopped at a cancel: neither a result nor a failure.
    bool cancelled = false;
};

}  // namespace

void run_batch(const std::vector<ScanItem>& items, const BatchRun& run,
               store::RecordStore& store, bool redo, int worker_count,
               const BatchCallbacks& callbacks) {
    const AnalysisSettings& settings = run.settings;
    const std::atomic<bool>* cancel = callbacks.cancel;

    // "Already has a result" means a current-version record at the cap this
    // run would produce (Auto: any record above 4 bars) AND under this run's
    // ms limit and score range, so stale rows, other caps' rows and other
    // settings' rows are re-run rather than skipped.
    const store::CapQuery cap = store::CapQuery::from_setting(settings.sp_cap);
    // One query for the whole library, not one per chart.
    const std::unordered_set<std::string> analyzed =
        redo ? std::unordered_set<std::string>{}
             : store.analyzed_hashes(run.chartmode, cap, run.lens);
    std::vector<const ScanItem*> todo;
    for (const ScanItem& item : items) {
        if (analyzed.count(item.md5)) continue;
        todo.push_back(&item);
    }

    BatchProgress progress;
    progress.total = static_cast<int>(todo.size());
    if (callbacks.on_progress) callbacks.on_progress(progress);
    if (todo.empty()) return;

    // A running search checks for cancel in its progress callback. Throwing
    // AnalysisCancelled there unwinds it at the next tick (the engine reports
    // every half percent of the chart), the way the single-chart Analyze
    // button stops. With no cancel flag there is nothing to check, so the
    // search gets no callback at all, exactly as before.
    std::function<void(float)> check_cancel;
    if (cancel)
        check_cancel = [cancel](float) {
            if (cancel->load(std::memory_order_relaxed)) throw AnalysisCancelled{};
        };
    const ChartAnalyzer analyze =
        callbacks.analyze ? callbacks.analyze : ChartAnalyzer(analyze_chart_file);

    int completed = 0;
    run_work_pool<WorkResult>(
        todo.size(), worker_count, cancel,
        [&](size_t i) {
            const ScanItem* item = todo[i];
            WorkResult wr;
            wr.item = *item;
            try {
                AnalysisResult ar = analyze(item->notespath, settings, check_cancel);
                wr.row = store::prepare_row(
                    store::RecordKey{item->md5, run.chartmode, cap, run.lens}, ar.record);
                wr.dynamics = dynamics_entry_from_analysis(
                    item->md5, ar.song, settings.bass2x, settings.difficulty,
                    settings.prodrums);
                wr.analysis = std::move(ar);
            } catch (const AnalysisCancelled&) {
                wr.cancelled = true;
            } catch (const std::exception& e) {
                wr.error = e.what();
            }
            return wr;
        },
        [&](WorkResult&& wr) {
            // Once cancel is seen nothing more is written or reported, as
            // before. A search stopped part-way is not a failure, and a result
            // that finished alongside the cancel is dropped.
            if (wr.cancelled || (cancel && cancel->load())) return;

            ++completed;
            if (!wr.error.empty()) {
                if (callbacks.on_error) callbacks.on_error(wr.item.title, wr.error);
            } else {
                store.save_analysis(wr.item.md5, wr.item.title, wr.item.artist,
                                    wr.item.charter, wr.analysis->song, *wr.row, wr.dynamics);
                if (callbacks.on_result) callbacks.on_result(wr.item, *wr.row);
            }

            progress.completed = completed;
            progress.current_title = wr.item.title;
            if (callbacks.on_progress) callbacks.on_progress(progress);
        });
}

}  // namespace hydra::app
