/**
 * @file cmd/rules.h
 * @brief keur rules — inspect the resolved rule cascade.
 *
 *   keur rules list     tree of every rule by layer, with its cascade
 *                       specificity and which rule wins an id collision.
 *
 * The layers mirror CSS specificity: built-in (weakest) -> global -> project
 * -> explicit override (strongest). When two layers define the same rule id,
 * the more specific layer wins, exactly like a later/more-specific CSS rule.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace keur::cmd {

struct RulesOptions {
  std::string subcommand = "list";   // "list" | "disable" | "enable"
  bool winners_only = false;          // --winners: show only the effective rule per id
  bool json = false;                  // --json: machine-readable list
  std::string rule_id;                // for disable/enable: the target id
  std::string scope = "repo";         // --scope repo|global (where to write)
};

RulesOptions rules_parse(const std::vector<std::string>& args);
int rules(const RulesOptions& opts);

/**
 * Toggle one rule on/off by writing the `[rules] disabled` array of the target
 * scope's keur.toml (repo = <git-root>/keur.toml, global = ~/.config/keur/…).
 * This is the SINGLE rule-write path: both `keur rules disable/enable` and the
 * interactive `keur manager` tree call it, so the CLI and TUI cannot drift.
 * Returns 0 on success, 1 on error. Prints a one-line confirmation.
 * @param disable  true = turn the rule off, false = turn it back on.
 * @param scope    "repo" (default) | "global".
 */
int rules_toggle(const std::string& id, bool disable, const std::string& scope);

/**
 * Toggle one external scanner (gitleaks/semgrep/…) on/off by writing
 * `[scanners.<name>] enabled = true|false` into the target scope's keur.toml,
 * preserving the rest of the file. Same single-write-path discipline as
 * rules_toggle: both the CLI and the `keur manager` tree call it. Keeps one
 * .bak backup. Returns 0 on success, 1 on error.
 * @param enable  true = turn the scanner on, false = off.
 * @param scope   "repo" (default) | "global".
 */
int scanners_toggle(const std::string& name, bool enable, const std::string& scope);

/**
 * Atomically apply a complete desired state to a scope's keur.toml in ONE
 * transaction (plan → apply, Terraform-style): set `[rules] disabled` to
 * exactly `disabled_rules`, and set each `[scanners.<name>] enabled` from
 * `scanner_enabled`. One read, one transform, one write, one .bak backup — not
 * many per-item edits. Creates the parent dir if missing. Returns 0 on success.
 * @param scope  "repo" | "global".
 */
int apply_scope(const std::string& scope,
                const std::vector<std::string>& disabled_rules,
                const std::vector<std::pair<std::string, bool>>& scanner_enabled);

}  // namespace keur::cmd
