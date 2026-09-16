/**
 * @file git.h
 * @brief Git adapter — the single touch point for git queries.
 *
 * Every git interaction goes through this adapter. Centralising it means:
 *   - one place that spawns git (one popen implementation, one quoting policy),
 *   - a stable seam for tests (inject a fake Git instead of shelling out),
 *   - no ad-hoc `popen("git ...")` scattered across subcommands.
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur {

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

    /** Real implementations (over the git CLI). */
    static std::string default_run(const std::string& args);
    static std::vector<std::string> default_staged_files();
};

/**
 * The .git/hooks directory of the current repo, or "" when not in a repo.
 * Uses `git rev-parse --git-dir` + "/hooks" so it ignores a global
 * core.hooksPath (per-repo installs must land in the actual repo).
 */
std::string git_repo_hooks_dir(const Git& git = Git{});

}  // namespace keur
