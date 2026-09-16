/**
 * @file cmd_hook.h
 * @brief keur hook — install / manage keur git hooks.
 *
 * Writes minimal one-line stubs into .git/hooks/ (per-repo) or the global
 * git hooks directory.  The stubs themselves are unavoidably shell (git calls
 * hook files as executables), but contain no logic — only `exec keur check`.
 */
#pragma once
#include <string>
#include <vector>

namespace keur {

enum class HookAction { Install, Global, Status, Remove };

struct HookOptions {
  HookAction  action   = HookAction::Install;
  bool        global   = false;  // applies to --remove --global
};

/** Parse --install / --global / --status / --remove [--global] from argv. */
HookOptions cmd_hook_parse(const std::vector<std::string>& args);

/** Run the hook subcommand. Returns 0 on success, 1 on error. */
int cmd_hook(const HookOptions& opts);

/**
 * Write hook stubs into a directory.
 * Creates parent directories if they do not exist.
 *
 * @param hooks_dir  Destination directory (e.g. .git/hooks or
 *                   ~/.config/git/hooks).
 * @returns true if all hooks were written successfully.
 */
bool hooks_install(const std::string& hooks_dir);

/**
 * Remove keur hook stubs from a directory (silently succeeds if absent).
 */
void hooks_remove(const std::string& hooks_dir);

/**
 * Return the path of the global git hooks directory.
 * Respects $XDG_CONFIG_HOME; defaults to ~/.config/git/hooks.
 */
std::string global_hooks_dir();

/**
 * Return the per-repo git hooks directory for the current working directory.
 * Returns an empty string when not inside a git repository.
 */
std::string repo_hooks_dir();

}  // namespace keur
