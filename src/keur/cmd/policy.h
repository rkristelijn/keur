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
 * Resolve the config path following keur's precedence order:
 *   builtin defaults → global ($XDG_CONFIG_HOME/keur/keur.toml) → repo
 *   (keur.toml in git root) → dir-local.
 *
 * @param hint  Optional explicit path (from --config flag); takes priority.
 * @returns the path of the first existing file, or the global path as
 *          a fallback placeholder.
 */
std::string resolve_config(const std::string& hint = "");

}  // namespace keur::cmd
