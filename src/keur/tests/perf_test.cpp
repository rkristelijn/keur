// keur:ignore-file SEC-041 — test support
/**
 * @file tests/perf_test.cpp
 * @brief Performance timing guards (doctest suite "perf").
 *
 * Split out of keur_test.cpp (ADR-002 test layout). No doctest main here — that
 * lives once in test_main.cpp; this TU only registers its scenarios. Shared
 * helpers come from test_helpers.h.
 */
#include <chrono>
#include <string>
#include <vector>

#include "../../../vendor/doctest.h"
#include "rules/rule_engine.h"
#include "cmd/policy.h"
#include "test_helpers.h"

using keur::test::keur_rules_dir;
using keur::test::fixtures_dir;

// ============================================================================
// TEST_SUITE: perf — timing guards
// ============================================================================

TEST_SUITE("perf") {

SCENARIO("[feature:rule-engine] rules_load is fast (<50ms)") {
    auto t0 = std::chrono::steady_clock::now();
    auto rules = keur::rules_load(keur_rules_dir());
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    MESSAGE("rules_load: " << ms << "ms, " << rules.size() << " rules");
    CHECK(ms < 50);
}

SCENARIO("[feature:rule-engine] scan 3 files is fast (<100ms)") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::vector<std::string> files = {
        fixtures_dir() + "/secrets/rsa_key.pem",
        fixtures_dir() + "/secrets/ec_key.pem",
        fixtures_dir() + "/clean/clean.yml",
    };
    auto t0 = std::chrono::steady_clock::now();
    auto fs = keur::rules_scan_files(rules, files);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    MESSAGE("scan 3 files: " << ms << "ms, " << fs.size() << " findings");
    CHECK(ms < 100);
}

SCENARIO("[feature:policy] 100x policy_load is fast (<5ms)") {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        keur::cmd::policy_load("/tmp/nonexistent_perf.toml");
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    MESSAGE("100x policy_load: " << us << "us");
    CHECK(us < 5000);
}

} // TEST_SUITE("perf")
