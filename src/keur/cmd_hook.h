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

#include "git.h"

namespace keur {

enum class HookAction { Install, Global, Status, Remove };

struct HookOptions {
  HookAction  action   = HookAction::Install;
  bool        global   = false;  // applies to --remove --global
};

/** Parse --install / --global / --status / --remove [--global] from argv. */
HookOptions cmd_hook_parse(const std::vector<std::string>& args);

/**
 * Run the hook subcommand. Returns 0 on success, 1 on error.
 *
 * @param opts  Parsed options.
 * @param git   Git adapter — defaults to the real git CLI. Tests inject a fake
 *              so no real repository or global config is touched.
 */
int cmd_hook(const HookOptions& opts, const Git& git = Git{});

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
 * @param git  Git adapter (injectable for tests).
 */
std::string repo_hooks_dir(const Git& git = Git{});

}  // namespace keur
