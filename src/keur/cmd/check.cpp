/**
 * @file cmd/check.cpp
 * @brief keur check — gate the staged diff (called by git hooks).
 *
 * Layers (SRP):
 *   1. staged_files()    — I/O: asks git for staged files
 *   2. policy_evaluate() — pure logic: signals → verdict (no I/O)
 *   3. check()       — orchestrator: wires 1+2+rule engine, prints result
 */
#include "cmd/check.h"

#include <iostream>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "cmd/policy.h"
#include "adapters/git.h"
#include "util/keur_utils.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// I/O boundary: git staged files — delegates to the git adapter so there is
// exactly one place in the codebase that spawns git.
// ---------------------------------------------------------------------------

std::vector<std::string> staged_files(const adapters::Git& git) {
    return git.staged_files();
}

// ---------------------------------------------------------------------------
// Pure function: policy evaluation (no I/O, table-testable)
// ---------------------------------------------------------------------------

Verdict policy_evaluate(const PolicyInput& input) {
    if (input.has_secret_block || input.has_rule_block) return Verdict::Block;
    if (input.has_any_finding)                          return Verdict::Warn;
    return Verdict::Pass;
}

// ---------------------------------------------------------------------------
// Arg parser
// ---------------------------------------------------------------------------

CheckOptions check_parse(const std::vector<std::string>& args) {
    CheckOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--tier" && i + 1 < args.size()) {
            ++i;
            o.tier = (args[i] == "pre-push") ? Tier::PrePush : Tier::PreCommit;
        } else if (args[i] == "--ai" && i + 1 < args.size()) {
            o.ai = args[++i];
        } else if (args[i] == "--config" && i + 1 < args.size()) {
            o.config = args[++i];
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Orchestrator
// ---------------------------------------------------------------------------

int check(const CheckOptions& opts, FileSource file_src) {
    const char* tier_str =
        (opts.tier == Tier::PrePush) ? "pre-push" : "pre-commit";

    // 1. File discovery (injectable — tests pass a lambda). When no source is
    //    given, fall back to staged_files() over the real git adapter.
    if (!file_src) file_src = []() { return staged_files(); };
    auto files = file_src();
    if (files.empty()) {
        std::cerr << "KEUR (" << tier_str << "): no staged files -- PASS\n";
        return 0;
    }
    std::cerr << "KEUR (" << tier_str << "): scanning "
              << files.size() << " staged file(s)...\n";

    // 2. Load rules
    std::vector<Rule> rules;
    for (auto& d : rule_dirs()) {
        auto rs = rules_load(d);
        for (auto& r : rs) rules.push_back(std::move(r));
    }

    // 3. Scan in-process
    PolicyInput input{};
    if (!rules.empty()) {
        auto findings = rules_scan_files(rules, files);
        for (auto& f : findings) {
            input.has_any_finding = true;
            if (f.action == "block") input.has_rule_block = true;
        }
    }

    // 4. Pure policy decision
    auto verdict = policy_evaluate(input);

    switch (verdict) {
        case Verdict::Block:
            std::cerr << "KEUR (" << tier_str
                      << "): BLOCK -- blocking finding(s) detected\n";
            return 1;
        case Verdict::Warn:
            std::cerr << "KEUR (" << tier_str
                      << "): WARN -- non-blocking finding(s); review advised\n";
            return 0;
        case Verdict::Pass:
            std::cerr << "KEUR (" << tier_str << "): PASS\n";
            return 0;
    }
    return 0;
}

}  // namespace keur::cmd
