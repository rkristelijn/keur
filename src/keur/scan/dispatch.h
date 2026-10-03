/**
 * @file scan/dispatch.h
 * @brief Scanner dispatch (ADR-019): run the enabled evidence sources over the
 *        staged files and return one aggregated list of canonical Findings.
 *
 * keur orchestrates proven tools (gitleaks, semgrep, …) as evidence sources and
 * fills gaps with its own RE2 rule engine. This unit is the missing dispatch
 * step: given the staged files, the resolved scanner inventory, and the adapter
 * seams (Proc, Git), it produces `std::vector<Finding>`. It does no I/O of its
 * own — every spawn / PATH-probe goes through the injected Proc, so the whole
 * orchestration is table-testable with fakes (no real gitleaks, no real files).
 *
 * Design invariants (do not break):
 *   - Adapters produce EVIDENCE only; the policy engine stays the sole referee
 *     (ADR-014). Nothing here decides PASS/WARN/BLOCK.
 *   - One binary, no runtime plugins (ADR-002): each proven tool is a fixed,
 *     compiled-in mapper, not a loadable module.
 *   - keur does not reimplement linters (ADR-001): it shells out to the proven
 *     tool (shell-free via Proc) and normalises the tool's JSON to Finding.
 *   - Missing tool → skipped gracefully (never blocks) — same as today's config.
 *
 * Repo-local tool config awareness: when a repo ships a tool's own config file
 * (e.g. `.gitleaks.toml`, `.semgrep.yml`) in the repo root OR under `.config/`,
 * keur passes it to the tool so the local run matches how the repo expects that
 * tool to behave. See resolve_tool_config().
 *
 * @implements ADR-019 ADR-003
 * @id KEUR-SCAN-DISPATCH
 *
 * Sub-concepts (declared here, implemented in mappers.cpp / dispatch.cpp):
 * @id KEUR-SCAN-DISPATCH.config-resolution
 * @id KEUR-SCAN-DISPATCH.gitleaks
 * @id KEUR-SCAN-DISPATCH.semgrep
 * @id KEUR-SCAN-DISPATCH.shellcheck
 * @id KEUR-SCAN-DISPATCH.project-local
 * @id ADR-019.finding-shape
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "adapters/git.h"
#include "adapters/proc.h"
#include "config/scanners.h"

namespace keur::scan {

/**
 * The canonical Finding shape — the common currency every evidence source
 * (proven tool, keur rule engine, project-local check) normalises to. The
 * policy engine folds over these; the `source` sets provenance/confidence but
 * never the verdict (ADR-014 invariant). @implements ADR-019.finding-shape
 */
struct Finding {
    std::string source;         ///< "gitleaks" | "semgrep" | "keur-rules" | …
    std::string rule;           ///< tool rule-id (e.g. "generic-api-key", "SEC-041")
    std::string title;
    std::string category;       ///< "secret" | "sast" | "iac" | …
    std::string severity;       ///< "critical"|"error"|"warning"|"info"
    std::string action;         ///< "block" | "warn" | "advisory"
    std::string confidence;     ///< "0.0".."1.0" (string to match rule engine output)
    bool        deterministic = true;
    std::string file;
    int         line = 0;
    std::string message;
    std::string fix;            ///< remediation hint (from the rule's fix: field)
};

/**
 * Resolve a scanner's repo-local config file to an absolute path, searching
 * (in order) the repo root then `.config/`. Returns "" when no known config
 * file for that scanner is present. Pure over the injected `exists` probe and
 * the candidate file names, so it is unit-testable with no real filesystem.
 *
 * Precedence rationale: a repo-local tool config is the most specific statement
 * of how that repo wants the tool to run, so it wins — the same "most specific
 * layer wins" idea keur already uses for rule dirs and keur.toml. Root is tried
 * before `.config/` because a root dotfile is the tool's documented default
 * location; `.config/` is the tidier alternative some repos prefer.
 *
 * @param candidates repo-relative names to try, in order (e.g.
 *                    {".gitleaks.toml"}); the same name is also tried under
 *                    ".config/". Typically `known_config_files(scanner)` minus
 *                    the ignore-only files.
 * @param repo_root  absolute repo root ("" → cwd-relative).
 * @param exists     file-existence probe (injected; spoofable in tests).
 * @returns the first existing path (root first, then .config/), or "".
 * @implements KEUR-SCAN-DISPATCH.config-resolution
 */
[[nodiscard]] std::string resolve_tool_config(
    const std::vector<std::string>& candidates,
    const std::string& repo_root,
    const std::function<bool(const std::string&)>& exists);

/**
 * Map gitleaks `detect --report-format json` output to Findings. Pure string →
 * vector transform; independently testable with a captured sample. Unparseable
 * input yields an empty vector (fail-open: a broken tool never fabricates a
 * blocking finding). @implements KEUR-SCAN-DISPATCH.gitleaks
 */
[[nodiscard]] std::vector<Finding> map_gitleaks_json(const std::string& json);

/**
 * Map semgrep `--json` output to Findings. Pure string → vector transform;
 * independently testable with a captured sample. @implements
 * KEUR-SCAN-DISPATCH.semgrep
 */
[[nodiscard]] std::vector<Finding> map_semgrep_json(const std::string& json);

/**
 * Map shellcheck `--format=json1` output ({"comments":[…]}) to Findings. A
 * shellcheck `level` of "error" maps to action=block, everything else (warning
 * /info/style) to warn — matching ADR-019's guidance that a lint code like
 * SC2034 should WARN, not overblock. Pure; captured-sample testable.
 * @implements KEUR-SCAN-DISPATCH.shellcheck
 */
[[nodiscard]] std::vector<Finding> map_shellcheck_json(const std::string& json);

/**
 * Parse KEUR Finding JSONL (one JSON object per line) as emitted by a
 * project-local check script's stdout. Confidence is capped at `cap` so a
 * repo-provided script cannot claim more evidence weight than a calibrated
 * proven tool (ADR-019 governance control). Unparseable lines are skipped.
 * @implements KEUR-SCAN-DISPATCH.project-local
 */
[[nodiscard]] std::vector<Finding> parse_finding_jsonl(const std::string& jsonl,
                                                       double cap);

/**
 * Injected seams + inputs for dispatch — everything the orchestration touches,
 * so tests supply fakes and nothing real is spawned or read.
 */
struct DispatchContext {
    std::vector<std::string>          files;        ///< staged files to scan
    std::vector<config::Scanner>      scanners;     ///< resolved inventory (enabled + on_path)
    std::string                       repo_root;    ///< for tool-config resolution
    adapters::Proc                    proc;         ///< spawn/PATH seam (fakeable)
    std::function<bool(const std::string&)> exists = nullptr;  ///< file probe (fakeable)
    /// Proven-tool names allowed to run in the active tier ([tiers.<t>] scanners).
    /// Keeps a heavy tool (semgrep) out of the fast pre-commit gate. Empty →
    /// no tier filter (every wired adapter may run) — the default for unit tests.
    std::vector<std::string> tier_allowed;
    /// keur-rules layer: injected so dispatch stays pure over it (the real call
    /// site wires in rules_scan_files via load_active_rules). Empty → skipped.
    std::function<std::vector<Finding>(const std::vector<std::string>&)> keur_rules = nullptr;

    // --- Project-local check adapter (ADR-019 shape B) ---------------------
    /// Whether the project-local check runs at all ([scanners.project-local]
    /// enabled). Default true (ADR-019: default-enabled, gated on git trust).
    bool project_local_enabled = true;
    /// Answers "is this repo-relative path a git-tracked file?" — the trust
    /// boundary: an untracked/dropped script is never run. Injected (fake Git
    /// in tests). Empty → treated as not-tracked (fail-closed).
    std::function<bool(const std::string&)> is_tracked = nullptr;
    /// Confidence cap for project-local findings (ADR-019 governance control).
    double project_local_cap = 0.80;
};

/**
 * Run every enabled + available scanner in the context over the staged files
 * and return the aggregated Findings. Order is presentation only (proven tools,
 * then keur-rules); the verdict is the policy engine's job downstream.
 *
 * A scanner is run only when `enabled` and `on_path`; a missing tool is skipped
 * (no finding, no error). Repo-local tool config (resolve_tool_config) is passed
 * to the tool when present. @implements KEUR-SCAN-DISPATCH
 */
[[nodiscard]] std::vector<Finding> dispatch_scanners(const DispatchContext& ctx);

}  // namespace keur::scan
