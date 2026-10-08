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

#include "common/platform.h"
#include "report/report.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "cmd/policy.h"
#include "adapters/git.h"
#include "adapters/fs.h"
#include "adapters/proc.h"
#include "config/ruleset.h"
#include "config/keurignore.h"
#include "config/scanners.h"
#include "config/scopes.h"
#include "scan/dispatch.h"
#include "util/keur_utils.h"

#include <optional>

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

// Branch-protection finding (KEUR-BRANCH-001). Pure: given the current and
// default branch names and the resolved maturity level, decide whether a direct
// commit to the default branch should be reported, and how hard. Gated by
// maturity exactly like a rule (ADR-026): warn from level >= 2 (developing),
// block from level >= 4 (managed). Below 2, or off the default branch, no
// finding. Emitting it as a normal Finding with a rule id means it flows through
// the SAME suppression (`[rules] disabled`, `keur:ignore KEUR-BRANCH-001`) as
// any rule — so a repo that does not want it can simply turn it off.
// @id KEUR-BRANCH-001 @implements ADR-057
std::optional<scan::Finding> branch_protection_finding(
    const std::string& current_branch, const std::string& default_branch, int level) {
    if (current_branch.empty() || default_branch.empty()) return std::nullopt;
    if (current_branch != default_branch) return std::nullopt;
    if (level < 2) return std::nullopt;  // prototype/developing-1: not gated yet

    const bool block = level >= 4;
    scan::Finding f;
    f.source = "keur";
    f.rule = "KEUR-BRANCH-001";
    f.title = "Direct commit to the default branch";
    f.category = "quality";
    f.severity = block ? "error" : "warning";
    f.action = block ? "block" : "warn";
    f.confidence = block ? "0.9" : "0.6";
    f.file = default_branch;
    f.line = 0;
    f.message = "Committing directly to the default branch '" + default_branch +
        "'. Work on a feature branch and open a merge/pull request so changes "
        "are reviewed before they land. (Disable with keur:ignore KEUR-BRANCH-001 "
        "or [rules] disabled if your workflow commits to the default branch.)";
    f.fix = "git switch -c <feature-branch>, commit there, then open an MR/PR.";
    return f;
}


std::string format_finding_line(const std::string& rule, const std::string& action,
                                const std::string& confidence, const std::string& file,
                                int line, const std::string& message, bool blocks,
                                const std::string& fix, bool color) {
    // Marker first so a scanning eye finds the blockers immediately. When colour
    // is on (a TTY, or FORCE_COLOR/CLICOLOR_FORCE — see report::use_color), the
    // marker is red for a blocker, yellow for a warning, so the verdict reads at
    // a glance in a terminal or a recording.
    const char* red    = color ? "\033[31m" : "";
    const char* yellow = color ? "\033[33m" : "";
    const char* reset  = color ? "\033[0m"  : "";
    std::string out = blocks ? (std::string(red) + "BLOCK " + reset)
                             : (std::string(yellow) + "warn  " + reset);
    out += rule.empty() ? "(no-rule)" : rule;
    out += " | " + (action.empty() ? std::string("warn") : action);
    out += " | conf " + (confidence.empty() ? std::string("?") : confidence);
    out += " | " + (file.empty() ? std::string("(no-file)") : file);
    if (line > 0) out += ":" + std::to_string(line);
    if (!message.empty()) out += " | " + message;
    // Self-service remediation: tell the committer (human OR AI) how to FIX it,
    // and — if it is a genuine false positive — exactly how to suppress it, so
    // nobody reaches for --no-verify. This is keur's core promise (vision).
    if (!fix.empty()) out += "\n        fix: " + fix;
    if (!rule.empty()) {
        out += "\n        false positive? add on that line a comment: keur:ignore " + rule;
    }
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
    const bool real_hook_run = !file_src;  // no injected source => a real commit
    if (!file_src) file_src = []() { return staged_files(); };
    auto files = file_src();
    if (files.empty()) {
        // Not an error: a clean pass must write to stdout. VS Code and some CI
        // treat ANY stderr output from a hook as a failure even on exit 0.
        std::cout << "KEUR (" << tier_str << "): no staged files -- PASS\n";
        return 0;
    }
    std::cout << "KEUR (" << tier_str << "): scanning "
              << files.size() << " staged file(s)...\n";

    // 2. Load the active rule set (resolved dirs minus the disable-list).
    //    Capture the resolved disabled-id set so we can also suppress matching
    //    findings from EXTERNAL tools (semgrep/… ) — a repo `[rules] disabled`
    //    entry then silences that id whatever source produced it.
    std::set<std::string> disabled_ids;
    std::vector<Rule> rules = keur::config::load_active_rules(&disabled_ids);

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
    probe.tool_on_path = [&proc](const std::string& t) { return proc.has(t); };
    probe.exists = [&fs](const std::string& p) { return fs.exists(p); };
    probe.repo_root = repo_root;
    auto scanners = config::resolve_scanners(declared, probe);

    scan::DispatchContext ctx;
    ctx.files = files;
    ctx.scanners = std::move(scanners);
    ctx.tier_allowed = std::move(tier_allowed);
    ctx.repo_root = repo_root;
    ctx.proc = proc;
    ctx.exists = [&fs](const std::string& p) { return fs.exists(p); };
    // keur-rules layer: run the in-process engine, mapped to canonical Findings.
    // File-level rules (file-absence/presence, etc.) are evaluated over the whole
    // repo tree (repo_root), not just the staged diff — a "repo has no
    // SECURITY.md" check is a repo property, not a diff property. Per-line rules
    // still run only over the staged files, so the hook stays sub-second.
    ctx.keur_rules = [&rules, repo_root](const std::vector<std::string>& fs_in) {
        std::vector<scan::Finding> out;
        if (rules.empty()) return out;
        for (const auto& rf : rules_scan_files_scoped(rules, fs_in, repo_root)) {
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
            f.fix = rf.fix;
            out.push_back(std::move(f));
        }
        return out;
    };
    // Project-local trust boundary: git ls-files --error-unmatch on the repo-
    // relative path; a tracked file returns non-empty (git prints the path).
    ctx.is_tracked = [&git](const std::string& rel) {
        return !git.run("ls-files --error-unmatch " + rel).empty();
    };

    auto findings = scan::dispatch_scanners(ctx);

    // Branch protection (KEUR-BRANCH-001, ADR-057): warn/block a direct commit
    // to the default branch, gated by the resolved maturity level — the same
    // config stack `keur difficulty` reads. Emitted as a normal Finding so it
    // runs through the suppression below and can be turned off like any rule.
    // Only on a real hook run: a test/explicit file_src must stay hermetic and
    // not depend on which branch the suite happens to run on.
    if (real_hook_run) {
        config::ScopeEnv denv = config::capture_scope_env("", repo_root);
        auto dlayers = config::resolve_layers(
            denv, [&fs](const std::string& p) { return fs.exists(p); });
        std::vector<std::pair<std::string, std::string>> dscopes;
        for (const auto& ly : dlayers)
            if (ly.exists && !ly.path.empty())
                dscopes.emplace_back(config::scope_name(ly.scope), ly.path);
        auto resolved = config::resolve_difficulty_explained(
            dscopes,
            [&fs](const std::string& p) { auto rr = fs.read_file(p); return rr.ok ? rr.content : std::string(); },
            /*default_level=*/1);
        // Branch discipline is a modifiability/process concern (ADR-039).
        int level = resolved.vector.level_of("modifiability");
        if (auto bf = branch_protection_finding(git.current_branch(),
                                                git.default_branch(), level))
            findings.push_back(std::move(*bf));
    }

    // Repo-level suppression on top of inline `keur:ignore` (ADR-035): the
    // `.keurignore` file (repo root or .config/). Secrets suppress by VALUE
    // (a moved secret stays hidden, a changed one is caught again); code issues
    // suppress by location/rule — a line-suppress can never silence a secret.
    // keur.toml `[rules] disabled` (rule id, any source) still applies too.
    auto ignore_entries = config::resolve_keurignore(
        repo_root, [&fs](const std::string& p) {
            auto r = fs.read_file(p);
            return r.ok ? r.content : std::string();
        });
    if (!disabled_ids.empty() || !ignore_entries.empty()) {
        std::vector<scan::Finding> kept;
        kept.reserve(findings.size());
        for (auto& f : findings) {
            if (disabled_ids.count(f.rule)) continue;
            if (config::keurignore_suppresses(ignore_entries, f.category, f.file,
                                              f.line, f.rule, f.message)) continue;
            kept.push_back(std::move(f));
        }
        findings = std::move(kept);
    }

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
        // Colour follows report::use_color: on a TTY, or forced via
        // FORCE_COLOR/CLICOLOR_FORCE (so the hook output is coloured in a
        // recording or a CI log too), unless NO_COLOR is set.
        bool color = report::use_color(report::ColorMode::Auto,
                                       keur::platform::stderr_is_tty());
        // A BLOCK is a real failure → stderr. A WARN is non-blocking (exit 0) →
        // stdout, so editors/CI do not misread an advisory as an error.
        std::ostream& out = (verdict == Verdict::Block) ? std::cerr : std::cout;
        for (const auto& f : findings) {
            bool blocks = finding_blocks(f.action, f.confidence, policy.block_at);
            out << "  " << format_finding_line(f.rule, f.action, f.confidence,
                                               f.file, f.line, f.message, blocks,
                                               f.fix, color)
                << "\n";
        }
    }

    switch (verdict) {
        case Verdict::Block:
            std::cerr << "KEUR (" << tier_str
                      << "): BLOCK -- blocking finding(s) above; commit stopped\n";
            return 1;
        case Verdict::Warn:
            std::cout << "KEUR (" << tier_str
                      << "): WARN -- non-blocking finding(s) above; review advised\n";
            return 0;
        case Verdict::Pass:
            std::cout << "KEUR (" << tier_str << "): PASS\n";
            return 0;
    }
    return 0;
}

}  // namespace keur::cmd
