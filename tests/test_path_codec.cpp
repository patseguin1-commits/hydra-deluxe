// Tests for store/path_codec.{h,cpp}: the content-addressed path codec.
//
// The cornerstone is an equality proxy, not a field checklist. A record taken
// apart by the new codec and put back together must serialize, through the
// untouched record serializer, to exactly the same bytes as the same record
// round-tripped through that serializer alone. If any field the codec stores,
// rebuilds, or drops were wrong, those bytes would differ.

#include "doctest.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"
#include "store/path_codec.h"
#include "store/record_store.h"
#include "store/serialize.h"

using namespace hydra;
using namespace hydra::store;

namespace {

// One analyzed corpus chart whose record exercises the whole codec: root
// paths with nested variants (so the structure blob has real tree shape) and
// a non-empty all-0 list (its own tree, stored after the roots).
struct CodecFixture {
    Song song;
    HydraRecord record;
};

const CodecFixture& fixture() {
    static CodecFixture f = [] {
        for (const std::string& path : corpus::chart_paths()) {
            Song s = load_songpath(path, true, true);
            if (s.is_empty()) continue;
            try {
                SearchSettings settings;
                settings.sp_cap = 4;
                settings.depth_mode = DepthMode::Scores;
                settings.depth_value = 4;
                settings.ms_filter = 10.0;
                HydraRecord r = analyze_chart(s, settings);
                if (r.paths.empty() || r.allzero_paths.empty()) continue;
                // all_paths() counts roots plus nested variants; more paths
                // than roots means at least one root has variants.
                if (r.all_paths().size() <= r.paths.size()) continue;
                return CodecFixture{std::move(s), std::move(r)};
            } catch (const ChartFileError&) {
                continue;
            }
        }
        throw std::runtime_error(
            "no corpus chart produced both variants and an all-0 path");
    }();
    return f;
}

// First field where two summaries differ, empty when equal.
std::string diff_summary(const PathSummary& a, const PathSummary& b) {
    if (a.score != b.score) return "score";
    if (a.actcount != b.actcount) return "actcount";
    if (a.maxskip != b.maxskip) return "maxskip";
    if (a.hardest_ms != b.hardest_ms) return "hardest_ms";
    if (a.avgmult != b.avgmult) return "avgmult";
    if (a.notecount != b.notecount) return "notecount";
    if (a.sqin_count != b.sqin_count) return "sqin_count";
    if (a.sqout_count != b.sqout_count) return "sqout_count";
    if (a.pathcount != b.pathcount) return "pathcount";
    return "";
}

// Every distinct node payload in a record, counted independently of the
// codec's own dedup bookkeeping.
void collect_payloads(const Path& path, std::unordered_set<std::string>& out) {
    std::vector<uint8_t> payload = encode_path_node(path);
    out.insert(std::string(payload.begin(), payload.end()));
    for (const Path& v : path.variants) collect_payloads(v, out);
}

std::unordered_set<std::string> distinct_payloads(const HydraRecord& record) {
    std::unordered_set<std::string> out;
    for (const Path& p : record.paths) collect_payloads(p, out);
    for (const Path& p : record.allzero_paths) collect_payloads(p, out);
    return out;
}

std::vector<std::string> pathstrings(const std::vector<const Path*>& paths) {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const Path* p : paths) out.push_back(p->pathstring());
    return out;
}

}  // namespace

TEST_CASE("path codec: a rebuilt record is byte-identical through the record serializer") {
    const HydraRecord& rec = fixture().record;
    REQUIRE_FALSE(rec.paths.empty());
    REQUIRE_FALSE(rec.allzero_paths.empty());
    REQUIRE(rec.all_paths().size() > rec.paths.size());

    // The two round trips: the old serializer alone, and the new codec.
    HydraRecord old_loaded = read_record(write_record(rec));
    FlatRecord flat = flatten_record(rec);
    HydraRecord new_loaded = rebuild_record(flat);

    // The strongest equality proxy available: every field any consumer reads
    // goes through write_record, so equal bytes means equal records.
    CHECK(write_record(old_loaded) == write_record(new_loaded));

    // Header fields.
    CHECK(new_loaded.ms_limit == old_loaded.ms_limit);
    CHECK(new_loaded.sp_cap == old_loaded.sp_cap);
    CHECK(new_loaded.sp_cap_converged == old_loaded.sp_cap_converged);
    CHECK(new_loaded.ms_limit == rec.ms_limit);
    CHECK(new_loaded.sp_cap == rec.sp_cap);
    CHECK(new_loaded.sp_cap_converged == rec.sp_cap_converged);

    // Counts, at the root level and across the whole variant tree.
    CHECK(new_loaded.paths.size() == old_loaded.paths.size());
    CHECK(new_loaded.allzero_paths.size() == old_loaded.allzero_paths.size());
    CHECK(new_loaded.all_paths().size() == old_loaded.all_paths().size());
    CHECK(new_loaded.all_allzero_paths().size() ==
          old_loaded.all_allzero_paths().size());

    // Per-path pathstrings, in traversal order.
    CHECK(pathstrings(new_loaded.all_paths()) ==
          pathstrings(old_loaded.all_paths()));
    CHECK(pathstrings(new_loaded.all_allzero_paths()) ==
          pathstrings(old_loaded.all_allzero_paths()));

    // The summary columns the library listing sorts on.
    CHECK(diff_summary(summarize_record(new_loaded),
                       summarize_record(old_loaded)) == "");
    CHECK(diff_summary(summarize_record(new_loaded), summarize_record(rec)) == "");

    // The tied-path recount is rebuilt, not stored; it must land the same way.
    for (size_t i = 0; i < new_loaded.paths.size(); ++i)
        CHECK(new_loaded.paths[i].tied_pathcount() ==
              old_loaded.paths[i].tied_pathcount());

    // Both sides carry raw ticks until timecodes are restored; after the same
    // restore against the song's timing, the strings still agree.
    restore_timecodes(old_loaded, fixture().song.timing());
    restore_timecodes(new_loaded, fixture().song.timing());
    CHECK(pathstrings(new_loaded.all_paths()) ==
          pathstrings(old_loaded.all_paths()));
    CHECK(pathstrings(new_loaded.all_allzero_paths()) ==
          pathstrings(old_loaded.all_allzero_paths()));
    CHECK(write_record(old_loaded) == write_record(new_loaded));
}

TEST_CASE("path codec: a record with no paths round-trips") {
    HydraRecord empty = fixture().record;
    empty.paths.clear();
    empty.allzero_paths.clear();

    FlatRecord flat = flatten_record(empty);
    CHECK(flat.nodes.empty());

    HydraRecord back = rebuild_record(flat);
    CHECK(back.paths.empty());
    CHECK(back.allzero_paths.empty());
    CHECK(back.ms_limit == empty.ms_limit);
    CHECK(back.sp_cap == empty.sp_cap);
    CHECK(back.sp_cap_converged == empty.sp_cap_converged);
    CHECK(write_record(back) == write_record(read_record(write_record(empty))));
}

TEST_CASE("path codec: node payloads are flat and content-addressed") {
    const HydraRecord& rec = fixture().record;

    // A node payload carries the node's own fields and nothing about the tree.
    // Take a root that actually has variants, so "flat" is a real claim.
    const Path* with_variants = nullptr;
    for (const Path& p : rec.paths)
        if (!p.variants.empty()) { with_variants = &p; break; }
    REQUIRE(with_variants != nullptr);
    const Path& root = *with_variants;
    Path node = decode_path_node(encode_path_node(root));
    CHECK(node.variants.empty());
    CHECK_FALSE(node.var_point.has_value());
    CHECK(node.activations.size() == root.activations.size());
    CHECK(node.multsqueezes.size() == root.multsqueezes.size());
    CHECK(node.score_base == root.score_base);
    CHECK(node.notecount == root.notecount);
    CHECK(node.leftover_sp == root.leftover_sp);
    CHECK(node.skipped_ghosts == root.skipped_ghosts);
    CHECK(node.skipped_accents == root.skipped_accents);

    // The hash is 32 lowercase hex characters, and it names the bytes: the
    // same payload always hashes the same, a different one does not.
    std::vector<uint8_t> payload = encode_path_node(root);
    const std::string hash = path_hash(payload);
    CHECK(hash.size() == 32);
    CHECK(hash.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(path_hash(payload) == hash);
    std::vector<uint8_t> tweaked = payload;
    tweaked.back() ^= 0x01;
    CHECK(path_hash(tweaked) != hash);

    // A malformed payload is refused, not misread.
    std::vector<uint8_t> bad_version = payload;
    bad_version[0] = static_cast<uint8_t>(kPathNodeFormatVersion + 1);
    CHECK_THROWS_AS(decode_path_node(bad_version), SerializeError);
    std::vector<uint8_t> truncated(payload.begin(), payload.begin() + 6);
    CHECK_THROWS_AS(decode_path_node(truncated), SerializeError);
}

TEST_CASE("path codec: flattening dedups and is stable") {
    const HydraRecord& rec = fixture().record;

    FlatRecord a = flatten_record(rec);
    FlatRecord b = flatten_record(rec);

    // Same record in, same structure blob and same node set out.
    CHECK(a.structure == b.structure);
    REQUIRE(a.nodes.size() == b.nodes.size());
    std::unordered_set<std::string> hashes_a, hashes_b;
    for (const StoredPathNode& n : a.nodes) hashes_a.insert(n.hash);
    for (const StoredPathNode& n : b.nodes) hashes_b.insert(n.hash);
    CHECK(hashes_a == hashes_b);

    // Each stored hash appears once, and the stored set is exactly the set of
    // distinct node payloads in the record — no duplicates, nothing missing.
    CHECK(hashes_a.size() == a.nodes.size());
    CHECK(a.nodes.size() == distinct_payloads(rec).size());

    // Every stored payload really is named by its hash.
    for (const StoredPathNode& n : a.nodes) CHECK(path_hash(n.payload) == n.hash);
}

TEST_CASE("path codec: a missing node or a bad structure blob throws") {
    const HydraRecord& rec = fixture().record;
    FlatRecord flat = flatten_record(rec);
    REQUIRE_FALSE(flat.nodes.empty());

    // A lookup that never resolves anything.
    CHECK_THROWS_AS(
        rebuild_record(flat.structure,
                       [](const std::string&) -> const std::vector<uint8_t>* {
                           return nullptr;
                       }),
        SerializeError);

    // One hash missing from an otherwise complete store.
    FlatRecord holed = flat;
    holed.nodes.erase(holed.nodes.begin());
    CHECK_THROWS_AS(rebuild_record(holed), SerializeError);

    // A structure blob from another format version, and a truncated one.
    FlatRecord future = flat;
    future.structure[0] = static_cast<uint8_t>(kPathStructureFormatVersion + 1);
    CHECK_THROWS_AS(rebuild_record(future), SerializeError);

    FlatRecord cut = flat;
    cut.structure.resize(cut.structure.size() / 2);
    CHECK_THROWS_AS(rebuild_record(cut), SerializeError);
}
