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
#include "adapters/fs.h"
#include "adapters/proc.h"
#include "config/ruleset.h"
#include "config/scanners.h"
#include "scan/dispatch.h"
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

// Pure threshold test — a block-action finding only blocks when its confidence
// reaches block_at. Malformed/empty confidence fails safe (blocks). This is how
// block-at in keur.toml governs the gate; a repo can raise block-at above 1.0
// to make keur report-only (everything degrades to WARN).
bool finding_blocks(const std::string& action, const std::string& confidence,
                    double block_at) {
    if (action != "block") return false;
    try {
        return std::stod(confidence) >= block_at;
    } catch (const std::exception&) {
        return true;  // fail safe: a blocking rule with a bad confidence blocks
    }
}

std::string format_finding_line(const std::string& rule, const std::string& action,
                                const std::string& confidence, const std::string& file,
                                int line, const std::string& message, bool blocks) {
    // Marker first so a scanning eye finds the blockers immediately.
    std::string out = blocks ? "BLOCK " : "warn  ";
    out += rule.empty() ? "(no-rule)" : rule;
    out += " | " + (action.empty() ? std::string("warn") : action);
    out += " | conf " + (confidence.empty() ? std::string("?") : confidence);
    out += " | " + (file.empty() ? std::string("(no-file)") : file);
    if (line > 0) out += ":" + std::to_string(line);
    if (!message.empty()) out += " | " + message;
    return out;
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

    // 2. Load the active rule set (resolved dirs minus the disable-list).
    std::vector<Rule> rules = keur::config::load_active_rules();

    // Resolve the policy thresholds (repo-local keur.toml wins over global).
    // block-at decides whether a blocking finding actually blocks; a repo can
    // set block-at above 1.0 to make keur report-only.
    std::string config_path = resolve_config(opts.config);
    auto policy = policy_load(config_path);

    // 3. Build the scanner-dispatch context (ADR-019): proven-tool adapters +
    //    keur-rules + project-local check, all over the real adapter seams.
    adapters::Git git;
    adapters::Fs  fs;
    std::string repo_root = git.run("rev-parse --show-toplevel");

    // Resolve the scanner inventory from keur.toml (or the baked default) and
    // enrich with PATH availability so missing tools skip gracefully.
    std::vector<config::Scanner> declared;
    std::vector<std::string> tier_allowed;
    {
        auto rr = fs.read_file(config_path);
        declared = rr.ok ? config::parse_scanners(rr.content)
                         : config::default_scanners();
        if (declared.empty()) declared = config::default_scanners();
        // Tier gate: only the scanners named for this tier run, so heavy tools
        // (semgrep) stay out of the fast pre-commit budget. Absent → empty →
        // dispatch applies no tier filter.
        if (rr.ok) tier_allowed = config::tier_scanners(rr.content, tier_str);
    }
    adapters::Proc proc;
    config::ScannerProbe probe;
    probe.tool_on_path = [&](const std::string& t) { return proc.has(t); };
    probe.exists = [&](const std::string& p) { return fs.exists(p); };
    probe.repo_root = repo_root;
    auto scanners = config::resolve_scanners(declared, probe);

    scan::DispatchContext ctx;
    ctx.files = files;
    ctx.scanners = std::move(scanners);
    ctx.tier_allowed = std::move(tier_allowed);
    ctx.repo_root = repo_root;
    ctx.proc = proc;
    ctx.exists = [&](const std::string& p) { return fs.exists(p); };
    // keur-rules layer: run the in-process engine, mapped to canonical Findings.
    ctx.keur_rules = [&](const std::vector<std::string>& fs_in) {
        std::vector<scan::Finding> out;
        if (rules.empty()) return out;
        for (const auto& rf : rules_scan_files(rules, fs_in)) {
            scan::Finding f;
            f.source = "keur-rules";
            f.rule = rf.rule_id;
            f.title = rf.title;
            f.category = rf.category;
            f.severity = rf.severity;
            f.action = rf.action;
            f.confidence = rf.confidence;
            f.file = rf.file;
            f.line = rf.line;
            f.message = rf.message;
            out.push_back(std::move(f));
        }
        return out;
    };
    // Project-local trust boundary: git ls-files --error-unmatch on the repo-
    // relative path; a tracked file returns non-empty (git prints the path).
    ctx.is_tracked = [&](const std::string& rel) {
        return !git.run("ls-files --error-unmatch " + rel).empty();
    };

    auto findings = scan::dispatch_scanners(ctx);

    // 4. Derive the pure policy input from the aggregated findings, then decide.
    //    ONE policy path (ADR-014): the verdict is a function of exactly the
    //    findings printed below — no second, hidden code path can block.
    PolicyInput input{};
    for (const auto& f : findings) {
        input.has_any_finding = true;
        if (finding_blocks(f.action, f.confidence, policy.block_at)) {
            input.has_rule_block = true;
            if (f.category == "secret") input.has_secret_block = true;
        }
    }
    auto verdict = policy_evaluate(input);

    // Self-explaining gate: on WARN/BLOCK, print the finding(s) so the verdict
    // is reproducible from what the user sees (fixes the "blocks with no reason"
    // usability bug). Each line marks whether that finding is a blocker.
    if (verdict != Verdict::Pass) {
        for (const auto& f : findings) {
            bool blocks = finding_blocks(f.action, f.confidence, policy.block_at);
            std::cerr << "  " << format_finding_line(f.rule, f.action, f.confidence,
                                                     f.file, f.line, f.message, blocks)
                      << "\n";
        }
    }

    switch (verdict) {
        case Verdict::Block:
            std::cerr << "KEUR (" << tier_str
                      << "): BLOCK -- blocking finding(s) above; commit stopped\n";
            return 1;
        case Verdict::Warn:
            std::cerr << "KEUR (" << tier_str
                      << "): WARN -- non-blocking finding(s) above; review advised\n";
            return 0;
        case Verdict::Pass:
            std::cerr << "KEUR (" << tier_str << "): PASS\n";
            return 0;
    }
    return 0;
}

}  // namespace keur::cmd
