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
#include <functional>
#include <map>
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

/// Parse a `[difficulty]` section from a keur.toml body into the vector's level
/// map (ADR-039 / DESIGN-DIFFRES.schema). Each `ility = N` sets level N (1..5),
/// `ility = "off"` sets level 0. Also reads `[difficulty.rules]` id = on|off
/// into `out_overrides` (true = forced on, false = forced off). Unset keys are
/// left untouched so a caller can layer global then repo. Pure.
/// @id DESIGN-DIFFRES.schema
void parse_difficulty(const std::string& toml_body, DifficultyVector& out_vec,
                      std::map<std::string, bool>& out_overrides);

/// Fold the resolved config layer stack into one DifficultyVector (the cascade,
/// DESIGN-DIFFRES.cascade): default_level, then per layer global→repo apply
/// `[policy] level` (all-equal shorthand, DESIGN-DIFFRES.compat) then
/// `[difficulty]` per key, then `[difficulty.rules]`. `read` returns a TOML
/// body for a layer path (""/missing = skipped). Pure given `read`.
/// @id DESIGN-DIFFRES.merge
[[nodiscard]] DifficultyVector merge_difficulty(
    const std::vector<std::string>& layer_paths_global_to_repo,
    const std::function<std::string(const std::string&)>& read,
    std::map<std::string, bool>& out_overrides, int default_level = 1);

/// Keep only rules active under `vec` (ADR-039 OR/max), then apply per-rule
/// overrides (forced on keeps/re-adds, forced off drops). Replaces the scalar
/// filter_by_maturity; that stays as a thin wrapper for back-compat. Pure.
/// @id DESIGN-DIFFRES.step.wire
[[nodiscard]] std::vector<Rule> filter_by_vector(
    std::vector<Rule> rules, const DifficultyVector& vec,
    const std::map<std::string, bool>& overrides = {});

/// One layer's contribution to the resolved difficulty (for `--explain`).
struct DifficultyLayer {
    std::string scope;   // "global" | "repo" | "explicit" | ...
    std::string path;
    int         level = 0;                               // [policy] level, 0 = none
    std::vector<std::pair<std::string, int>> ilities;    // [difficulty] keys set here
    std::vector<std::pair<std::string, bool>> rules;     // [difficulty.rules] here
};

/// Full resolution with a per-layer trace — the data behind `keur difficulty
/// --explain`. Reads the real config layer stack via `read`/`exists`.
/// @id DESIGN-DIFFRES.explain
struct DifficultyResolved {
    DifficultyVector vector;
    std::map<std::string, bool> overrides;
    std::vector<DifficultyLayer> layers;   // global -> repo order
    int default_level = 1;
};
[[nodiscard]] DifficultyResolved resolve_difficulty_explained(
    const std::vector<std::pair<std::string, std::string>>& scope_paths_global_to_repo,
    const std::function<std::string(const std::string&)>& read,
    int default_level = 1);

/// Load all rules from the resolved rule dirs, then remove the disabled ones.
/// Resolves the disable-list from the real config layer stack (global/repo/…).
/// `out_disabled`, when non-null, receives the disabled-id set that was applied
/// (so callers like `rules list` can show which ids are off).
[[nodiscard]] std::vector<Rule> load_active_rules(std::set<std::string>* out_disabled = nullptr);

}  // namespace keur::config
