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
     * True when a merge is in progress (`.git/MERGE_HEAD` present). On a merge
     * commit `git diff --cached` reports the ENTIRE merged tree, so the scope
     * selector (check.cpp) uses this to switch to merge_scoped_files().
     * @implements ADR-068
     */
    std::function<bool()> merge_in_progress =
        default_merge_in_progress;

    /**
     * Merge-scoped staged files: only what THIS developer's side CONTRIBUTED to
     * the merge — NOT the whole incoming tree. Computed as the merge-base
     * three-dot diff intersected with the staged set:
     *   base   = git merge-base HEAD MERGE_HEAD
     *   scoped = (git diff --name-only -z base...HEAD) ∩ staged_files()
     * This is the ADR-068 Decision 2 correction of the iteration-1 query
     * `--cached --diff-filter=M`, which on a WIDE merge wrongly returned every
     * file both sides touched (measured: 50 vs the correct 9). Falls back to the
     * full staged set when MERGE_HEAD / merge-base cannot be resolved (never
     * crash, never silently scan nothing). NUL-safe, same -z pattern as
     * staged_files(). @implements ADR-068
     */
    std::function<std::vector<std::string>()> merge_scoped_files =
        default_merge_scoped_files;

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
    static bool default_merge_in_progress();
    static std::vector<std::string> default_merge_scoped_files();
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
