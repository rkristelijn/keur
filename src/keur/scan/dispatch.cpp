/**
 * @file scan/dispatch.cpp
 * @brief Scanner dispatch orchestration (ADR-019). Runs the enabled evidence
 *        sources (proven-tool adapters → keur-rules → project-local check) over
 *        the staged files and aggregates canonical Findings. Pure over the
 *        injected Proc/Git seams; the JSON→Finding mappers live in mappers.cpp.
 *        @implements KEUR-SCAN-DISPATCH
 */
#include "scan/dispatch.h"

#include <cstdio>

#include "common/platform.h"

namespace keur::scan {

std::string resolve_tool_config(
    const std::vector<std::string>& candidates,
    const std::string& repo_root,
    const std::function<bool(const std::string&)>& exists) {
    if (!exists) return "";
    auto full = [&repo_root](const std::string& rel) {
        return repo_root.empty() ? rel : platform::path_join(repo_root, rel);
    };
    // Root first (the tool's documented default location), then .config/.
    for (const auto& rel : candidates) {
        std::string p = full(rel);
        if (exists(p)) return p;
    }
    for (const auto& rel : candidates) {
        std::string p = full(platform::path_join(".config", rel));
        if (exists(p)) return p;
    }
    return "";
}

namespace {

// Config-file candidates to PASS to a tool (the config, not the ignore file).
// Ignore files are read by the tool automatically from the repo root; only the
// primary config is worth an explicit --config so .config/ placement works too.
std::vector<std::string> config_candidates(const std::string& scanner) {
    if (scanner == "gitleaks") return {".gitleaks.toml"};
    if (scanner == "semgrep")  return {".semgrep.yml", ".semgrep.yaml"};
    // shellcheck auto-reads .shellcheckrc; no --config flag to forward.
    return {};
}

// Which scanner names have a proven-tool adapter wired in dispatch.
bool is_adapter(const std::string& name) {
    return name == "gitleaks" || name == "semgrep" || name == "shellcheck";
}

// Is a scanner permitted in the active tier? An empty tier list = no filter
// (every wired adapter may run) — the default for unit tests.
bool tier_permits(const std::vector<std::string>& tier_allowed, const std::string& name) {
    if (tier_allowed.empty()) return true;
    for (const auto& n : tier_allowed) if (n == name) return true;
    return false;
}

// Restrict the file list to what a tool can meaningfully scan. shellcheck only
// understands shell scripts; every scanner here is invoked per file, so
// filtering avoids spawning a tool on a file it cannot parse. gitleaks/semgrep
// accept any file, so they get the list unchanged.
std::vector<std::string> filter_files(const std::string& scanner,
                                      const std::vector<std::string>& files) {
    if (scanner != "shellcheck") return files;
    std::vector<std::string> sh;
    for (const auto& f : files) {
        // shellcheck-relevant extensions only. Explicit (not a shebang sniff)
        // keeps the function pure over the path — no file read.
        if (f.size() >= 3 && f.compare(f.size() - 3, 3, ".sh") == 0) sh.push_back(f);
        else if (f.size() >= 5 && f.compare(f.size() - 5, 5, ".bash") == 0) sh.push_back(f);
    }
    return sh;
}

// Build the argv for a proven tool over ONE staged file, injecting the repo-
// local config with the tool's own flag when one was resolved. Per-file (not
// whole-tree) keeps the gate scoped to the staged diff, the same scope as the
// rule engine — a whole-tree scan would flag pre-existing, unrelated findings.
std::vector<std::string> build_argv(const config::Scanner& s,
                                    const std::string& tool_config,
                                    const std::string& file) {
    std::vector<std::string> argv;
    const std::string bin = s.bin.empty() ? s.name : s.bin;
    if (s.name == "gitleaks") {
        argv = {bin, "detect", "--no-git", "--source", file,
                "--report-format", "json", "--report-path", "-"};
        if (!tool_config.empty()) { argv.push_back("--config"); argv.push_back(tool_config); }
    } else if (s.name == "semgrep") {
        argv = {bin, "--json", "--quiet"};
        if (!tool_config.empty()) { argv.push_back("--config"); argv.push_back(tool_config); }
        else { argv.push_back("--config"); argv.push_back("auto"); }
        argv.push_back(file);
    } else if (s.name == "shellcheck") {
        argv = {bin, "--format=json1", file};
    }
    return argv;
}

std::vector<Finding> map_tool_output(const std::string& scanner, const std::string& out) {
    if (scanner == "gitleaks")   return map_gitleaks_json(out);
    if (scanner == "semgrep")    return map_semgrep_json(out);
    if (scanner == "shellcheck") return map_shellcheck_json(out);
    return {};
}

// Run ONE proven-tool adapter over its (filtered) staged files and return the
// findings. Extracted from the dispatch loop so that loop stays shallow (guard
// clauses over deep nesting — KEUR-QUAL-004).
std::vector<Finding> run_adapter(const config::Scanner& s, const DispatchContext& ctx) {
    std::vector<Finding> out;
    std::string tool_config =
        resolve_tool_config(config_candidates(s.name), ctx.repo_root, ctx.exists);
    for (const auto& file : filter_files(s.name, ctx.files)) {
        auto argv = build_argv(s, tool_config, file);
        if (argv.empty()) continue;
        auto res = ctx.proc.capture_argv ? ctx.proc.capture_argv(argv)
                                         : adapters::ProcResult{};
        for (auto& f : map_tool_output(s.name, res.stdout_text))
            out.push_back(std::move(f));
    }
    return out;
}

// Resolve the project-local check script: .keur/check.sh then
// .config/keur/check.sh, returning the first that EXISTS and is git-TRACKED.
// Tracked-only is the trust boundary (ADR-019): a dropped/untracked script in a
// cloned repo is ignored. Returns "" when none qualifies.
std::string resolve_project_script(const DispatchContext& ctx) {
    const std::vector<std::string> rels = {".keur/check.sh", ".config/keur/check.sh"};
    for (const auto& rel : rels) {
        std::string full = ctx.repo_root.empty()
                               ? rel : platform::path_join(ctx.repo_root, rel);
        bool present = ctx.exists && ctx.exists(full);
        bool tracked = ctx.is_tracked && ctx.is_tracked(rel);
        if (present && tracked) return full;
    }
    return "";
}

// Run the resolved project-local script (if any) shell-free with the staged
// files on argv. A non-zero exit is itself a BLOCK finding ("the check failed"),
// so a repo gets a working gate without printing any JSON. If the script also
// emits KEUR Finding JSONL on stdout, those are parsed and merged (confidence-
// capped). @implements KEUR-SCAN-DISPATCH
std::vector<Finding> run_project_local(const DispatchContext& ctx) {
    std::string script = resolve_project_script(ctx);
    if (script.empty()) return {};

    std::vector<std::string> argv = {script};
    for (const auto& f : ctx.files) argv.push_back(f);
    adapters::ProcResult res =
        ctx.proc.capture_argv ? ctx.proc.capture_argv(argv) : adapters::ProcResult{};

    // Findings the script explicitly emitted (capped).
    std::vector<Finding> out = parse_finding_jsonl(res.stdout_text, ctx.project_local_cap);

    // Exit-code contract: non-zero = the check failed = block the commit, even
    // when the script printed nothing structured. Capped like any project-local
    // evidence so it blocks on its own action but cannot outrank a proven tool.
    if (res.exit_code != 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", ctx.project_local_cap);
        Finding f;
        f.source = "project-local";
        f.rule = "project-local-check";
        f.title = "project-local check failed";
        f.category = "project";
        f.severity = "error";
        f.action = "block";
        f.confidence = buf;
        f.deterministic = true;
        f.message = "project-local check script exited non-zero (" +
                    std::to_string(res.exit_code) + ") — commit blocked";
        out.push_back(std::move(f));
    }
    return out;
}

// Should this scanner's proven-tool adapter run in this dispatch? Named so the
// dispatch loop reads as a guard clause, not a nested condition (KEUR-QUAL-004).
bool adapter_runs(const config::Scanner& s, const DispatchContext& ctx) {
    return s.enabled && s.on_path && is_adapter(s.name)
           && tier_permits(ctx.tier_allowed, s.name);
}

}  // namespace

std::vector<Finding> dispatch_scanners(const DispatchContext& ctx) {
    std::vector<Finding> all;
    auto add = [&all](std::vector<Finding> fs) {
        for (auto& f : fs) all.push_back(std::move(f));
    };

    // Layer 1: proven-tool adapters. Enabled + on PATH + permitted by the tier;
    // missing/filtered → skipped. Order is presentation only; the policy engine
    // is the sole referee downstream (ADR-014).
    for (const auto& s : ctx.scanners)
        if (adapter_runs(s, ctx)) add(run_adapter(s, ctx));

    // Layer 2: keur's own rule engine (gap-filler). Injected so this unit stays
    // pure over it; the real call site wires in rules_scan_files.
    if (ctx.keur_rules) add(ctx.keur_rules(ctx.files));

    // Layer 3: project-local check (ADR-019 shape B) — a git-tracked repo script
    // that can WARN or BLOCK on its own evidence, confidence-capped so it never
    // outweighs a calibrated proven tool.
    if (ctx.project_local_enabled) add(run_project_local(ctx));

    return all;
}

}  // namespace keur::scan
