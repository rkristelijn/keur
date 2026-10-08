/**
 * @file cmd/check.h
 * @brief keur check — gate the staged diff (called by git hooks).
 *
 * SRP breakdown:
 *   - staged_files()     : reads git state (injectable for tests)
 *   - policy_evaluate()  : pure function — input struct → verdict
 *   - check()        : orchestrates the above; injectable file source
 *
 * Testability: the heavy lifting (policy_evaluate) is a pure function.
 * check accepts an optional file-source function so tests can inject
 * a fixed file list without touching git or the filesystem.
 */
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "adapters/git.h"
#include "scan/dispatch.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** Which git hook tier fired. */
enum class Tier { PreCommit, PrePush };

/** Verdict produced by the policy engine — pure output, no side effects. */
enum class Verdict { Pass, Warn, Block };

/**
 * Aggregated scanner signals fed into the policy engine.
 * Boolean flags so adding new scanner types doesn't change the interface.
 */
struct PolicyInput {
    bool has_secret_block = false;
    bool has_rule_block   = false;
    bool has_any_finding  = false;
};

/** Options parsed from argv. */
struct CheckOptions {
    Tier        tier   = Tier::PreCommit;
    std::string ai     = "off";
    std::string config;
};

// ---------------------------------------------------------------------------
// Pure functions (fully testable, no I/O)
// ---------------------------------------------------------------------------

/** Parse --tier / --ai / --config from an argv slice. */
CheckOptions check_parse(const std::vector<std::string>& args);

/**
 * Pure policy evaluation: no I/O, no globals.
 * Takes scanner signals and returns a verdict.
 * Easy to table-test with every combination of inputs.
 */
Verdict policy_evaluate(const PolicyInput& input);

/**
 * Pure threshold test: does one finding block the commit?
 * A finding blocks only when its action is "block" AND its confidence is at or
 * above block_at. A malformed/empty confidence fails safe (treated as blocking).
 * This is what makes block-at in keur.toml actually govern the gate.
 */
bool finding_blocks(const std::string& action, const std::string& confidence,
                    double block_at);

/**
 * Branch-protection finding (KEUR-BRANCH-001). Pure: given the current branch,
 * the repo's default branch, and the resolved maturity level, return a Finding
 * when a direct commit to the default branch should be reported. Gated by
 * maturity (ADR-026/ADR-057): nullopt below level 2 or off the default branch;
 * action "warn" at level 2-3, "block" at level >= 4. Emitted as a normal
 * Finding with a rule id so it flows through the usual suppression and can be
 * turned off like any rule.
 */
std::optional<scan::Finding> branch_protection_finding(
    const std::string& current_branch, const std::string& default_branch, int level);

/**
 * Format one finding as a self-explaining gate line:
 *   "<mark> <rule> | <action> | conf <c> | <file>:<line> | <message>"
 *   "        fix: <fix>"                              (if the rule ships a fix)
 *   "        false positive? add on that line: keur:ignore <rule>"
 * A blocked commit must tell a human OR an AI exactly what to fix and how to
 * suppress a genuine false positive — so nobody reaches for --no-verify
 * (README/vision). Pure — table-testable.
 * @implements KEUR-SCAN-DISPATCH
 */
std::string format_finding_line(const std::string& rule, const std::string& action,
                                const std::string& confidence, const std::string& file,
                                int line, const std::string& message, bool blocks,
                                const std::string& fix = "", bool color = false);

// ---------------------------------------------------------------------------
// I/O boundary (injectable for tests)
// ---------------------------------------------------------------------------

/**
 * Return staged files from git (added/copied/modified).
 * Returns empty vector when not in a git repo or nothing is staged.
 * This is the I/O boundary — inject a fake adapters::Git in tests.
 * @param git  adapters::Git adapter (defaults to the real git CLI).
 */
std::vector<std::string> staged_files(const adapters::Git& git = adapters::Git{});

/**
 * A FileSource is any callable that returns a list of files to check.
 * Production code passes staged_files; tests pass a lambda with fixed data.
 */
using FileSource = std::function<std::vector<std::string>()>;

// ---------------------------------------------------------------------------
// Orchestrator
// ---------------------------------------------------------------------------

/**
 * Run the check subcommand.
 *
 * @param opts       Parsed options.
 * @param file_src   File source. When empty (default), staged_files() over the
 *                   real git is used. Tests inject a lambda with fixed data so
 *                   no git repository is required.
 * @returns 0 (PASS/WARN) or 1 (BLOCK).
 */
int check(const CheckOptions& opts, FileSource file_src = {});

}  // namespace keur::cmd
