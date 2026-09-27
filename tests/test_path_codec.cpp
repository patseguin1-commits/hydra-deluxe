// Tests for store/path_codec.{h,cpp}: the content-addressed path codec.
//
// The cornerstone is an equality proxy, not a field checklist. A record taken
// apart by the codec and put back together must flatten again to exactly the
// same bytes as the original (record_bytes.h). If any field the codec stores,
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
#include "record_bytes.h"
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

TEST_CASE("path codec: a rebuilt record flattens to the same bytes") {
    const HydraRecord& rec = fixture().record;
    REQUIRE_FALSE(rec.paths.empty());
    REQUIRE_FALSE(rec.allzero_paths.empty());
    REQUIRE(rec.all_paths().size() > rec.paths.size());

    HydraRecord back = rebuild_record(flatten_record(rec));
    CHECK(record_bytes(back) == record_bytes(rec));

    CHECK(back.ms_limit == rec.ms_limit);
    CHECK(back.sp_cap == rec.sp_cap);
    CHECK(back.sp_cap_converged == rec.sp_cap_converged);
    CHECK(back.rules_fingerprint == rec.rules_fingerprint);
    CHECK(back.multsqueezes == rec.multsqueezes);

    CHECK(back.all_paths().size() == rec.all_paths().size());
    CHECK(back.all_allzero_paths().size() == rec.all_allzero_paths().size());
    CHECK(pathstrings(back.all_paths()) == pathstrings(rec.all_paths()));
    CHECK(pathstrings(back.all_allzero_paths()) == pathstrings(rec.all_allzero_paths()));
    CHECK(diff_summary(summarize_record(back), summarize_record(rec)) == "");

    // Every path, variants included, carries its root's totals again after
    // prepare_variants pushes them down.
    const std::vector<const Path*> want = rec.all_paths();
    const std::vector<const Path*> got = back.all_paths();
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK(got[i]->totalscore() == want[i]->totalscore());
        CHECK(got[i]->notecount == want[i]->notecount);
        CHECK(got[i]->leftover_sp == want[i]->leftover_sp);
        CHECK(got[i]->tied_pathcount() == want[i]->tied_pathcount());
    }

    const ActivationWalk acts = back.best_path().walk_activations();
    REQUIRE_FALSE(acts.empty());
    CHECK(acts.front().deact_tick == rec.best_path().walk_activations().front().deact_tick);

    // Raw ticks until restored; after the restore the strings still agree.
    restore_timecodes(back, fixture().song.timing());
    CHECK(pathstrings(back.all_paths()) == pathstrings(rec.all_paths()));
    CHECK(record_bytes(back) == record_bytes(rec));
}

// A node is a path's activations and nothing else. Totals live with the
// root in the structure blob, and the squeezes live on the record.
TEST_CASE("path codec: a node carries activations only, never totals") {
    const Path& root = fixture().record.best_path();
    Path changed = root;
    changed.score_base += 1;
    changed.score_sp += 7;
    changed.notecount += 1;
    changed.leftover_sp += 1;
    CHECK(encode_path_node(changed) == encode_path_node(root));

    REQUIRE_FALSE(changed.activations.empty());
    changed.activations.front().skips += 1;
    CHECK(encode_path_node(changed) != encode_path_node(root));
}

TEST_CASE("path codec: root totals ride in the structure, once per root") {
    HydraRecord rec = fixture().record;
    const FlatRecord before = flatten_record(rec);
    rec.paths.front().score_base += 1;
    rec.paths.front().notecount += 2;
    rec.paths.front().leftover_sp += 3;
    const FlatRecord after = flatten_record(rec);

    CHECK(after.structure != before.structure);
    REQUIRE(after.nodes.size() == before.nodes.size());
    for (size_t i = 0; i < after.nodes.size(); ++i)
        CHECK(after.nodes[i].hash == before.nodes[i].hash);

    const HydraRecord back = rebuild_record(after);
    CHECK(back.paths.front().score_base == rec.paths.front().score_base);
    CHECK(back.paths.front().notecount == rec.paths.front().notecount);
    CHECK(back.paths.front().leftover_sp == rec.paths.front().leftover_sp);
}

TEST_CASE("path codec: the multiplier squeezes are stored once per record") {
    HydraRecord rec = fixture().record;
    Chord c;
    c.add_note(NoteColor::Red);
    c.add_note(NoteColor::Yellow);
    c.apply_cymbal(NoteColor::Yellow);
    rec.multsqueezes = {MultSqueeze(c, 8), MultSqueeze(c, 18)};
    HydraRecord none = rec;
    none.multsqueezes.clear();

    const FlatRecord with = flatten_record(rec);
    const FlatRecord without = flatten_record(none);
    // Each squeeze costs its chord code (a 4-byte length plus 5 characters)
    // and its 4-byte combo, once, however many paths the record holds.
    CHECK(with.structure.size() == without.structure.size() + 2 * 13);
    REQUIRE(with.nodes.size() == without.nodes.size());
    for (size_t i = 0; i < with.nodes.size(); ++i)
        CHECK(with.nodes[i].payload == without.nodes[i].payload);
    CHECK(rebuild_record(with).multsqueezes == rec.multsqueezes);
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
    CHECK(record_bytes(back) == record_bytes(empty));
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

    // deact_tick is the newest field on Activation (blob v4 / node v2); a
    // plain encode_path_node/decode_path_node round trip must keep it, not
    // just the fields that existed before it.
    REQUIRE_FALSE(root.activations.empty());
    REQUIRE(root.activations.front().deact_tick.has_value());
    CHECK(node.activations.front().deact_tick == root.activations.front().deact_tick);

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
    bad_version[0] = static_cast<uint8_t>(kPathFormatStamp.written + 1);
    CHECK_THROWS_AS(decode_path_node(bad_version), SerializeError);
    std::vector<uint8_t> truncated(payload.begin(), payload.begin() + 6);
    CHECK_THROWS_AS(decode_path_node(truncated), SerializeError);
}

// Only the current node layout is read. A node from an older layout is
// reachable only through an older structure, which the store never decodes.
TEST_CASE("path codec: a node in an older layout is rejected") {
    std::vector<uint8_t> old = encode_path_node(fixture().record.best_path());
    old[0] = 5;  // the 1.8.1 node layout
    CHECK_THROWS_AS(decode_path_node(old), SerializeError);
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
    future.structure[0] = static_cast<uint8_t>(kPathFormatStamp.written + 1);
    CHECK_THROWS_AS(rebuild_record(future), SerializeError);

    // Versions 1, 2 and 3 are real old versions, not just "some other
    // number": the structure format was bumped through 1 -> 2 -> 3 -> 4, and
    // the old layouts are refused the same as any unknown one.
    FlatRecord past1 = flat;
    past1.structure[0] = 1;
    CHECK_THROWS_AS(rebuild_record(past1), SerializeError);

    FlatRecord past2 = flat;
    past2.structure[0] = 2;
    CHECK_THROWS_AS(rebuild_record(past2), SerializeError);

    FlatRecord past3 = flat;
    past3.structure[0] = 3;
    CHECK_THROWS_AS(rebuild_record(past3), SerializeError);

    // Version 4 carried the old lookup-table chord codes.
    FlatRecord past4 = flat;
    past4.structure[0] = 4;
    CHECK_THROWS_AS(rebuild_record(past4), SerializeError);

    // Version 5 is the 1.8.1 layout: per-path squeezes and totals in nodes.
    FlatRecord past5 = flat;
    past5.structure[0] = 5;
    CHECK_THROWS_AS(rebuild_record(past5), SerializeError);

    // The current version is 6, and the unmodified flat record -- still at
    // that version -- round-trips through rebuild_record without throwing,
    // rules fingerprint included.
    CHECK(kPathFormatStamp.written == 6);
    CHECK(flat.structure[0] == static_cast<uint8_t>(kPathFormatStamp.written));
    HydraRecord rebuilt = rebuild_record(flat);
    CHECK(rebuilt.rules_fingerprint == rec.rules_fingerprint);
    CHECK(rebuilt.paths.size() == rec.paths.size());
    CHECK(rebuilt.allzero_paths.size() == rec.allzero_paths.size());

    FlatRecord cut = flat;
    cut.structure.resize(cut.structure.size() / 2);
    CHECK_THROWS_AS(rebuild_record(cut), SerializeError);
}
