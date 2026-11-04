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
#include <cstdlib>
#include <cctype>

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
// Merge-aware scoping (ADR-068 Decision 2) — pure helpers, table-testable.
// ---------------------------------------------------------------------------

std::optional<MergeScope> parse_merge_scope(const std::string& value) {
    // Case-insensitive, trimmed. Unknown/empty → nullopt so the lower config
    // layer survives (default → keur.toml → env → flag).
    std::string v;
    for (char c : value) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!v.empty()) break;  // trim leading; stop at trailing ws
            continue;
        }
        v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (v == "resolved") return MergeScope::Resolved;
    if (v == "all")      return MergeScope::All;
    if (v == "skip")     return MergeScope::Skip;
    return std::nullopt;
}

std::string merge_scope_name(MergeScope scope) {
    switch (scope) {
        case MergeScope::Resolved: return "resolved";
        case MergeScope::All:      return "all";
        case MergeScope::Skip:     return "skip";
    }
    return "resolved";
}

std::optional<MergeScope> parse_merge_scope_toml(const std::string& toml_body) {
    // Section-aware line scan mirroring parse_scanners/tier_scanners: the
    // `merge_scope` key only counts while inside [pre_commit]. A bracketed
    // header that is not [pre_commit] leaves the section, so a stray
    // `merge_scope =` under another table is ignored.
    bool in_section = false;
    size_t pos = 0;
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    while (pos < toml_body.size()) {
        size_t nl = toml_body.find('\n', pos);
        std::string line = (nl == std::string::npos)
            ? toml_body.substr(pos)
            : toml_body.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? toml_body.size() : nl + 1;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[') {
            // A table header may carry a trailing comment (`[pre_commit]  # x`).
            // Strip it before matching so the comment does not defeat the
            // section check. The header itself never contains a '#'.
            std::string head = t;
            auto hc = head.find('#');
            if (hc != std::string::npos) head = trim(head.substr(0, hc));
            in_section = (head == "[pre_commit]");
            continue;
        }
        if (!in_section) continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        if (trim(t.substr(0, eq)) != "merge_scope") continue;
        // Value: strip quotes (single OR double) + inline comment, then parse.
        std::string rhs = trim(t.substr(eq + 1));
        if (!rhs.empty() && (rhs.front() == '"' || rhs.front() == '\'')) {
            char quote = rhs.front();
            rhs = rhs.substr(1);
            auto q = rhs.find(quote);
            if (q != std::string::npos) rhs = rhs.substr(0, q);
        } else {
            auto h = rhs.find('#');
            if (h != std::string::npos) rhs = trim(rhs.substr(0, h));
        }
        return parse_merge_scope(rhs);
    }
    return std::nullopt;
}

MergeScope resolve_merge_scope(const std::string& toml_body,
                               const std::string& env_value,
                               std::optional<MergeScope> flag_override) {
    // Single-body convenience wrapper: the one-layer case of the layered
    // resolver (default (Resolved) → keur.toml [pre_commit] → env → flag). Kept
    // as one implementation so the precedence logic lives in exactly one place
    // (SonarCloud duplication fix) — a one-element body list IS the layered fold.
    return resolve_merge_scope_layered({toml_body}, env_value, flag_override);
}

std::optional<MergeScope> resolve_merge_scope_toml_layered(
    const std::vector<std::string>& bodies_global_to_repo) {
    // Fold global→repo: a later layer (repo) that sets a recognised value wins
    // over an earlier one (global); an absent/unrecognised key leaves the lower
    // layer's value intact. Mirrors config::merge_difficulty's cascade so
    // merge_scope honours the SAME global→repo stack as every other [section]
    // key (the TASK B requirement: adjustable at global OR repo scope).
    std::optional<MergeScope> scope;
    for (const auto& body : bodies_global_to_repo)
        if (auto t = parse_merge_scope_toml(body)) scope = *t;
    return scope;
}

MergeScope resolve_merge_scope_layered(
    const std::vector<std::string>& bodies_global_to_repo,
    const std::string& env_value, std::optional<MergeScope> flag_override) {
    // Full precedence chain:
    //   default (Resolved) < global keur.toml < repo keur.toml < env < flag.
    MergeScope scope = MergeScope::Resolved;                       // code default
    if (auto t = resolve_merge_scope_toml_layered(bodies_global_to_repo))
        scope = *t;                                                // global→repo toml
    if (auto e = parse_merge_scope(env_value)) scope = *e;         // env one-shot
    if (flag_override)                         scope = *flag_override;  // flag
    return scope;
}

std::vector<std::string> select_scoped_files(
    MergeScope scope, bool merge_in_progress,
    const std::vector<std::string>& full_staged,
    const std::vector<std::string>& merge_scoped) {
    switch (scope) {
        case MergeScope::All:
            return full_staged;                 // today's behaviour, always full
        case MergeScope::Skip:
            return merge_in_progress ? std::vector<std::string>{} : full_staged;
        case MergeScope::Resolved:
        default:
            return merge_in_progress ? merge_scoped : full_staged;
    }
}

// Does the gate still have work to do when the scoped file set is empty? An
// empty scope normally short-circuits to PASS, but branch protection
// (KEUR-BRANCH-001) is a property of the BRANCH, not the staged files — a merge
// whose resolved scope is empty (this side contributed nothing) is still a
// direct commit that must be gated. So the early PASS is taken ONLY when there
// are no files AND no branch finding. Pure: table-testable, and it is the exact
// ordering guarantee behind Fix 2 (branch protection evaluated BEFORE the
// empty-scope return). @implements ADR-057
bool early_pass_on_empty_scope(bool files_empty, bool has_branch_finding) {
    return files_empty && !has_branch_finding;
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
        } else if (args[i] == "--merge-scope" && i + 1 < args.size()) {
            // One-shot override (ADR-068); an unrecognised value is ignored so
            // the lower config layers still resolve. A value starting with "--"
            // is almost certainly the NEXT flag (the user forgot the argument),
            // so reject it rather than swallow a flag as a bogus scope value.
            const std::string& val = args[++i];
            if (val.rfind("--", 0) != 0)
                o.merge_scope_override = parse_merge_scope(val);
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Orchestrator
// ---------------------------------------------------------------------------

// Evaluate branch protection (KEUR-BRANCH-001, ADR-057) over the real adapters:
// resolve the maturity level from the full config stack and ask
// branch_protection_finding whether a direct commit to the default branch
// should warn/block. Extracted so the orchestrator can run it BEFORE the
// empty-scope early return — a merge whose resolved scope is empty (committer's
// side added nothing) must still be gated for branch discipline, which is a
// property of the BRANCH, not of the staged file set. Returns nullopt when no
// finding applies (off the default branch, level too low, detached HEAD).
// @implements ADR-057
std::optional<scan::Finding> evaluate_branch_protection(
    const adapters::Git& git, adapters::Fs& fs, const std::string& repo_root) {
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
    return branch_protection_finding(git.current_branch(), git.default_branch(), level);
}

int check(const CheckOptions& opts, FileSource file_src) {
    const char* tier_str =
        (opts.tier == Tier::PrePush) ? "pre-push" : "pre-commit";

    // 1. File discovery (injectable — tests pass a lambda). When no source is
    //    given, apply ADR-068 merge-aware scoping over the real git adapter: on
    //    a merge commit `git diff --cached` reports the whole merged tree, so
    //    the resolved `merge_scope` selects the full / conflict-resolved / empty
    //    set. An injected file_src stays hermetic (tests drive the scope logic
    //    through the pure helpers), reproducing the pre-ADR-068 behaviour.
    const bool real_hook_run = !file_src;  // no injected source => a real commit
    if (!file_src) {
        // Resolve merge_scope across the FULL config stack (ADR-068 Decision 3):
        // code default < global keur.toml < repo keur.toml < env < flag. We read
        // EVERY layer global→repo (config::resolve_layers — the same cascade
        // [difficulty]/disabled use) rather than only the single winning path,
        // so a sensible default set in the GLOBAL keur.toml is honoured even when
        // a repo keur.toml exists but does not set merge_scope. This is the
        // multi-user requirement: adjustable at global OR repo scope.
        adapters::Fs fsr;
        adapters::Git groot;
        std::string repo_root = groot.run("rev-parse --show-toplevel");
        config::ScopeEnv senv = config::capture_scope_env(opts.config, repo_root);
        auto layers = config::resolve_layers(
            senv, [&fsr](const std::string& p) { return fsr.exists(p); });
        std::vector<std::string> bodies;  // global→repo (resolve_layers order)
        for (const auto& ly : layers) {
            if (!ly.exists || ly.path.empty()) continue;
            auto rr = fsr.read_file(ly.path);
            if (rr.ok) bodies.push_back(rr.content);
        }
        const char* env = std::getenv("KEUR_MERGE_SCOPE");
        MergeScope scope = resolve_merge_scope_layered(
            bodies, env ? std::string(env) : std::string(),
            opts.merge_scope_override);
        file_src = [scope]() {
            adapters::Git g;
            bool merging = g.merge_in_progress();
            // Only pay for the second git call when it can change the result.
            std::vector<std::string> scoped =
                (merging && scope == MergeScope::Resolved) ? g.merge_scoped_files()
                                                           : std::vector<std::string>{};
            return select_scoped_files(scope, merging, g.staged_files(), scoped);
        };
    }
    auto files = file_src();

    // Branch protection (KEUR-BRANCH-001, ADR-057) is evaluated FIRST, before the
    // empty-scope early return. A merge commit whose resolved scope is empty (this
    // side contributed no file of its own, or merge_scope=skip) still represents a
    // direct commit to a branch, and branch discipline is a property of the branch
    // — not of the staged file set. Running it after the `files.empty()` return
    // (the pre-fix order) let an empty-scope merge slip past branch protection.
    // Only on a real hook run: an injected file_src must stay hermetic and not
    // depend on which branch the suite happens to run on. @implements ADR-057
    std::optional<scan::Finding> branch_finding;
    if (real_hook_run) {
        adapters::Git bgit;
        adapters::Fs  bfs;
        std::string broot = bgit.run("rev-parse --show-toplevel");
        branch_finding = evaluate_branch_protection(bgit, bfs, broot);
    }

    if (early_pass_on_empty_scope(files.empty(), branch_finding.has_value())) {
        // Not an error: a clean pass must write to stdout. VS Code and some CI
        // treat ANY stderr output from a hook as a failure even on exit 0.
        std::cout << "KEUR (" << tier_str << "): no staged files -- PASS\n";
        return 0;
    }
    if (!files.empty())
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

    // Branch protection (KEUR-BRANCH-001, ADR-057) was evaluated up front (before
    // the empty-scope early return) so an empty-scope merge is still gated. Fold
    // that finding in here so it flows through the SAME suppression below and can
    // be turned off like any rule.
    if (branch_finding) findings.push_back(std::move(*branch_finding));

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
