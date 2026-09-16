/**
 * @file cmd_check.h
 * @brief keur check — gate the staged diff (called by git hooks).
 *
 * SRP breakdown:
 *   - staged_files()     : reads git state (injectable for tests)
 *   - policy_evaluate()  : pure function — input struct → verdict
 *   - cmd_check()        : orchestrates the above; injectable file source
 *
 * Testability: the heavy lifting (policy_evaluate) is a pure function.
 * cmd_check accepts an optional file-source function so tests can inject
 * a fixed file list without touching git or the filesystem.
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "git.h"

namespace keur {

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
CheckOptions cmd_check_parse(const std::vector<std::string>& args);

/**
 * Pure policy evaluation: no I/O, no globals.
 * Takes scanner signals and returns a verdict.
 * Easy to table-test with every combination of inputs.
 */
Verdict policy_evaluate(const PolicyInput& input);

// ---------------------------------------------------------------------------
// I/O boundary (injectable for tests)
// ---------------------------------------------------------------------------

/**
 * Return staged files from git (added/copied/modified).
 * Returns empty vector when not in a git repo or nothing is staged.
 * This is the I/O boundary — inject a fake Git in tests.
 * @param git  Git adapter (defaults to the real git CLI).
 */
std::vector<std::string> staged_files(const Git& git = Git{});

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
int cmd_check(const CheckOptions& opts, FileSource file_src = {});

}  // namespace keur
