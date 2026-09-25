/**
 * @file config/ruleset.h
 * @brief Load the ACTIVE rule set: every rule from the resolved rule dirs,
 *        minus the ones disabled in the config layer stack (KEUR-DISABLED).
 *
 * This is the single place that combines rule loading with the disable-list, so
 * `check`, `scan`, `rules`, and `manager` all see the same active set — no
 * drift. The pure filter (drop ids in a set) is separated from the I/O
 * (resolving dirs + reading config), so the filter is unit-testable directly.
 * @implements ADR-003 KEUR-DISABLED
 */
#pragma once
#include <set>
#include <string>
#include <vector>

#include "rules/rule_engine.h"

namespace keur::config {

/// Drop every rule whose id is in `disabled`. Pure. Returns the kept rules.
[[nodiscard]] std::vector<Rule> filter_disabled(std::vector<Rule> rules,
                                                const std::set<std::string>& disabled);

/// Keep only rules that gate at or below the project's maturity `level` (ADR-026):
/// a rule is active when its maturity <= level. A rule with maturity 0 (unset)
/// is treated as the default band `default_maturity` (3, defined). `level <= 0`
/// means "no maturity filter" (keep all) — the backwards-compatible default when
/// no [policy] level is configured. Pure — unit-testable.
/// @id KEUR-MATURITY.filter
[[nodiscard]] std::vector<Rule> filter_by_maturity(std::vector<Rule> rules, int level,
                                                   int default_maturity = 3);

/// Read `[policy] level = N` from a keur.toml body (the value `keur level`
/// writes). Returns 0 when absent/unparseable = "no maturity filter". Pure.
/// @id KEUR-MATURITY.level
[[nodiscard]] int policy_level(const std::string& toml_body);

/// Load all rules from the resolved rule dirs, then remove the disabled ones.
/// Resolves the disable-list from the real config layer stack (global/repo/…).
/// `out_disabled`, when non-null, receives the disabled-id set that was applied
/// (so callers like `rules list` can show which ids are off).
[[nodiscard]] std::vector<Rule> load_active_rules(std::set<std::string>* out_disabled = nullptr);

}  // namespace keur::config
