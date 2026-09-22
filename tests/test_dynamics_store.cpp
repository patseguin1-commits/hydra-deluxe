// Tests for dynamics storage: encode/decode round-trip and RecordStore
// put_dynamics/get_dynamics persistence.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/dynamics_breakdown.h"
#include "store/record_store.h"

using namespace hydra::app;
using namespace hydra::store;

namespace {

// Build a breakdown with non-zero counts in every row (both dynamics_enabled
// values are tested separately).
DynamicsBreakdown make_full_breakdown(bool dynamics_enabled) {
    DynamicsBreakdown b;
    b.dynamics_enabled = dynamics_enabled;
    int v = 1;
    for (size_t i = 0; i < static_cast<size_t>(DynamicsRow::Count); ++i) {
        b.rows[i].ghost  = v++;
        b.rows[i].accent = v++;
        b.rows[i].normal = v++;
    }
    return b;
}

// A temporary file path that is deleted on destruction.
struct TempFile {
    std::string path;
    TempFile() {
        char buf[MAX_PATH + 1];
        char dir[MAX_PATH + 1];
        GetTempPathA(MAX_PATH, dir);
        GetTempFileNameA(dir, "hyd", 0, buf);
        path = buf;
    }
    ~TempFile() { std::remove(path.c_str()); }
};

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: encode then decode round-trips with dynamics_enabled true and false.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics encode/decode round-trip") {
    for (bool enabled : {true, false}) {
        CAPTURE(enabled);
        const DynamicsBreakdown original = make_full_breakdown(enabled);
        const std::vector<uint8_t> blob = encode_dynamics(original);
        const auto decoded = decode_dynamics(blob);
        REQUIRE(decoded.has_value());
        CHECK(decoded->dynamics_enabled == enabled);
        for (size_t i = 0; i < static_cast<size_t>(DynamicsRow::Count); ++i) {
            CAPTURE(i);
            CHECK(decoded->rows[i].ghost  == original.rows[i].ghost);
            CHECK(decoded->rows[i].accent == original.rows[i].accent);
            CHECK(decoded->rows[i].normal == original.rows[i].normal);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 2: decode rejects empty blob and unknown version.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics decode rejects bad blobs") {
    SUBCASE("empty blob") {
        CHECK_FALSE(decode_dynamics({}).has_value());
    }
    SUBCASE("version 2 blob") {
        // Build a valid-length blob but stamp it version 2.
        const DynamicsBreakdown b = make_full_breakdown(true);
        std::vector<uint8_t> blob = encode_dynamics(b);
        blob[0] = 2;
        CHECK_FALSE(decode_dynamics(blob).has_value());
    }
    SUBCASE("truncated blob") {
        const DynamicsBreakdown b = make_full_breakdown(true);
        std::vector<uint8_t> blob = encode_dynamics(b);
        blob.resize(10);  // too short
        CHECK_FALSE(decode_dynamics(blob).has_value());
    }
}

// ---------------------------------------------------------------------------
// Test 3: RecordStore put/get dynamics — missing key, put+get, replace, and
//         keys differing only in pro or difficulty are separate rows.
// ---------------------------------------------------------------------------

TEST_CASE("RecordStore dynamics put/get") {
    TempFile tmp;
    const DynamicsBreakdown bd = make_full_breakdown(true);
    const std::vector<uint8_t> blob = encode_dynamics(bd);

    {
        RecordStore store(tmp.path);

        // Missing key returns nullopt.
        DynamicsKey key{"abc123", "Expert", false};
        CHECK_FALSE(store.get_dynamics(key).has_value());

        // Put then get returns the same bytes.
        store.put_dynamics(key, blob);
        auto got = store.get_dynamics(key);
        REQUIRE(got.has_value());
        CHECK(*got == blob);

        // Put again replaces (different blob).
        const DynamicsBreakdown bd2 = make_full_breakdown(false);
        const std::vector<uint8_t> blob2 = encode_dynamics(bd2);
        store.put_dynamics(key, blob2);
        got = store.get_dynamics(key);
        REQUIRE(got.has_value());
        CHECK(*got == blob2);

        // A key differing only in pro is a separate row.
        DynamicsKey key_pro{"abc123", "Expert", true};
        CHECK_FALSE(store.get_dynamics(key_pro).has_value());
        store.put_dynamics(key_pro, blob);
        CHECK(store.get_dynamics(key_pro).has_value());
        // The non-pro row is still the replaced blob2.
        CHECK(*store.get_dynamics(key) == blob2);

        // A key differing only in difficulty is a separate row.
        DynamicsKey key_hard{"abc123", "Hard", false};
        CHECK_FALSE(store.get_dynamics(key_hard).has_value());
        store.put_dynamics(key_hard, blob);
        CHECK(store.get_dynamics(key_hard).has_value());
    }

    // Test 4: Reopen the same db file and the rows are still there.
    {
        RecordStore store2(tmp.path);
        DynamicsKey key{"abc123", "Expert", false};
        auto got = store2.get_dynamics(key);
        REQUIRE(got.has_value());
        // Should be blob2 (the replaced value).
        const DynamicsBreakdown bd2 = make_full_breakdown(false);
        CHECK(*got == encode_dynamics(bd2));

        DynamicsKey key_pro{"abc123", "Expert", true};
        CHECK(store2.get_dynamics(key_pro).has_value());

        DynamicsKey key_hard{"abc123", "Hard", false};
        CHECK(store2.get_dynamics(key_hard).has_value());
    }
}
