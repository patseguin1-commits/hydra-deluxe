// Parity: the generated chord encode table must equal golden/chord_encode.json
// entry-for-entry. Catches a codegen bug in tools/gen_chord_tables.py or a
// stale chord_tables.cpp.

#include "doctest.h"

#include <cstdint>
#include <string>

#include "core/chord_tables.h"
#include "golden_util.h"

TEST_CASE("chord_encode matches golden entry-for-entry") {
    const golden::json expected = golden::chord_encode();
    const auto& table = hydra::chord_encode();

    CHECK(table.size() == expected.size());

    // Every golden entry is present with the same string.
    int missing = 0, wrong = 0;
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        int64_t hash = std::stoll(it.key());
        const std::string want = it.value().get<std::string>();
        const std::string* got = hydra::encode_chord(hash);
        if (!got) {
            if (++missing <= 5)
                CHECK_MESSAGE(got != nullptr, "missing hash " << hash);
        } else if (*got != want) {
            if (++wrong <= 5)
                CHECK_MESSAGE(*got == want, "hash " << hash << " -> '" << *got
                                                    << "' expected '" << want << "'");
        }
    }
    CHECK(missing == 0);
    CHECK(wrong == 0);

    // And the table has no extra entries the golden lacks.
    int extra = 0;
    for (const auto& kv : table) {
        if (!expected.contains(std::to_string(kv.first)) && ++extra <= 5)
            CHECK_MESSAGE(false, "extra hash " << kv.first);
    }
    CHECK(extra == 0);
}
