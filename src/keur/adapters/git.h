/**
 * @file adapters/git.h
 * @brief Git adapter — the single touch point for git queries.
 *
 * Every git interaction goes through this adapter. Centralising it means:
 *   - one place that spawns git (one popen implementation, one quoting policy),
 *   - a stable seam for tests (inject a fake Git instead of shelling out),
 *   - no ad-hoc `popen("git ...")` scattered across subcommands.
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 * @implements ADR-003
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur::adapters {

/**
 * Git queries used by keur. Implemented once in git.cpp over the real git CLI;
 * tests substitute a fake by assigning the function members.
 */
struct Git {
    /**
     * Run `git <args>` and return trimmed stdout (empty on error / not a repo).
     * `args` is appended verbatim after "git "; callers pass fixed strings only.
     */
    std::function<std::string(const std::string& args)> run =
        default_run;

    /**
     * Staged files (added/copied/modified), NUL-safe.
     * Empty when not in a repo or nothing staged.
     */
    std::function<std::vector<std::string>()> staged_files =
        default_staged_files;

    /**
     * The current branch name (e.g. "main"), or "" when detached/not a repo.
     */
    std::function<std::string()> current_branch =
        default_current_branch;

    /**
     * The repo's default branch (e.g. "main"/"master"), resolved from
     * origin/HEAD when known, else the local HEAD's default. "" if unknown.
     */
    std::function<std::string()> default_branch =
        default_default_branch;

    /** Real implementations (over the git CLI). */
    static std::string default_run(const std::string& args);
    static std::vector<std::string> default_staged_files();
    static std::string default_current_branch();
    static std::string default_default_branch();
};

/**
 * The .git/hooks directory of the current repo, or "" when not in a repo.
 * Uses `git rev-parse --git-dir` + "/hooks" so it ignores a global
 * core.hooksPath (per-repo installs must land in the actual repo).
 */
std::string git_repo_hooks_dir(const Git& git = Git{});

}  // namespace keur::adapters
