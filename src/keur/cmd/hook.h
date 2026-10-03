/**
 * @file cmd/hook.h
 * @brief keur hook — install / manage keur git hooks.
 *
 * Writes minimal one-line stubs into .git/hooks/ (per-repo) or the global
 * git hooks directory.  The stubs themselves are unavoidably shell (git calls
 * hook files as executables), but contain no logic — only `exec keur check`.
 */
#pragma once
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "adapters/proc.h"

namespace keur::cmd {

enum class HookAction { Install, Global, Status, Remove, Augment };

struct HookOptions {
  HookAction  action   = HookAction::Install;
  bool        global   = false;  // applies to --remove --global
  bool        remove_augment = false;  // --augment --remove: strip the keur block
};

/**
 * Bundle of injectable adapters for the hook subcommand.
 * Defaults are the real git / filesystem / process implementations.
 * Tests assign fakes to any member so nothing real is touched.
 */
struct HookDeps {
    adapters::Git  git{};
    adapters::Fs   fs{};
    adapters::Proc proc{};
};

/** Parse --install / --global / --status / --remove [--global] from argv. */
HookOptions hook_parse(const std::vector<std::string>& args);

/**
 * Run the hook subcommand. Returns 0 on success, 1 on error.
 *
 * @param opts  Parsed options.
 * @param deps  Injectable adapters (git / fs / proc). Defaults are real;
 *              tests inject fakes so no repo, files, or processes are touched.
 */
int hook(const HookOptions& opts, const HookDeps& deps = HookDeps{});

/**
 * Write hook stubs into a directory (via the adapters::Fs adapter).
 * @param hooks_dir  Destination directory.
 * @param fs         Filesystem adapter (injectable for tests).
 * @param global     When true, write the orchestrator stub (composes keur with
 *                   the repo's own .githooks per its keur.toml mode). When
 *                   false, write the minimal per-repo stub.
 * @returns true if all hooks were written successfully.
 */
bool hooks_install(const std::string& hooks_dir, const adapters::Fs& fs = adapters::Fs{},
                   bool global = false);

/**
 * Remove keur hook stubs from a directory (silently succeeds if absent).
 * @param fs  Filesystem adapter (injectable for tests).
 */
void hooks_remove(const std::string& hooks_dir, const adapters::Fs& fs = adapters::Fs{});

/**
 * Return the path of the global git hooks directory.
 * Respects $XDG_CONFIG_HOME; defaults to ~/.config/git/hooks.
 */
std::string global_hooks_dir();

/**
 * Return the per-repo git hooks directory for the current working directory.
 * Returns an empty string when not inside a git repository.
 * @param git  adapters::Git adapter (injectable for tests).
 */
std::string repo_hooks_dir(const adapters::Git& git = adapters::Git{});

/**
 * Insert a guarded keur-check line at the TOP of an existing hook script
 * (a manager's hook, e.g. .husky/pre-commit), so keur runs first — before the
 * manager's own checks (fail-fast). PURE: takes the file's current content,
 * returns the augmented content. Properties:
 *   - Idempotent: if the `# keur-augment` marker is already present, returns
 *     the content unchanged.
 *   - keur-only guard: the inserted line runs keur ONLY when it is on PATH
 *     (`command -v keur`), so a teammate without keur is never blocked — the
 *     line is safe to commit team-wide.
 *   - POSIX sh: works wherever git runs (git supplies the shell, incl.
 *     Git-Bash on Windows).
 * Inserted after a leading `#!` shebang if present, else at the very top.
 * @param content  the hook file's current text ("" for a new file).
 * @param tier     "pre-commit" | "pre-push".
 */
[[nodiscard]] std::string augment_hook(const std::string& content, const std::string& tier);

/// Remove a previously inserted keur-augment block from a hook script (the
/// marked lines). Idempotent: returns content unchanged if no marker. Pure.
[[nodiscard]] std::string unaugment_hook(const std::string& content);

/// True when the content already contains a keur-augment block. Pure.
[[nodiscard]] bool is_augmented(const std::string& content);

}  // namespace keur::cmd
