/**
 * @file cmd/policy.h
 * @brief keur policy — explain the resolved policy and thresholds.
 *
 * Reads keur.toml from the config resolution chain and prints the effective
 * policy settings to stdout (human-readable).
 */
#pragma once
#include <string>
#include <vector>

namespace keur::cmd {

struct PolicyOptions {
  std::string config;    // path to keur.toml (empty = auto-resolve)
  std::string subcommand = "explain";  // only "explain" for now
};

/** Parse [explain] and flags from argv. */
PolicyOptions policy_parse(const std::vector<std::string>& args);

/**
 * Run the policy subcommand.
 * Returns 0 on success.
 */
int policy(const PolicyOptions& opts);

/**
 * Resolved policy settings loaded from keur.toml.
 */
struct PolicySettings {
  double      block_at   = 0.85;
  double      warn_at    = 0.50;
  std::string version;
  std::string config_path;
};

/**
 * Load policy settings from the given TOML file.
 * Returns defaults if the file does not exist or a key is missing.
 */
PolicySettings policy_load(const std::string& config_path);

/**
 * Search for a repo-local `keur.toml` by walking up from `start_dir` to the
 * filesystem root. Returns the first existing path, or "" if none is found.
 * Pure (takes the start directory explicitly) so it is unit-testable without
 * depending on the process working directory.
 */
std::string find_config_upward(const std::string& start_dir);

/**
 * Resolve the config path following keur's precedence order:
 *   explicit --config → repo-local (keur.toml walking up from cwd) →
 *   global ($XDG_CONFIG_HOME/keur/keur.toml) → $KEUR_HOME/defaults/keur.toml.
 *
 * @param hint  Optional explicit path (from --config flag); takes priority.
 * @returns the path of the first existing file, or the global path as
 *          a fallback placeholder.
 */
std::string resolve_config(const std::string& hint = "");

}  // namespace keur::cmd
