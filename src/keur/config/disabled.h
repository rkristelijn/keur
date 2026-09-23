/**
 * @file config/disabled.h
 * @brief The rule disable-list: which rule ids are switched off, and where.
 *
 * A rule is turned off without touching its .rule file by listing its id in a
 * `[rules] disabled = [...]` array in a keur.toml layer. The engine loads every
 * rule as before, then this filter drops the disabled ones. Non-destructive
 * (the .rule files stay put, works for read-only rule dirs) and reviewable (the
 * off-switch is one diffable line in config). @see docs/design/keur-manager-tree-tui.md
 *
 * Resolution is layered like the rest of config: a disabled id in ANY resolved
 * layer counts (later layers add to the set; there is no "re-enable" that a
 * more-specific layer can express yet — that is a future refinement).
 *
 * Pure + injectable: toml bodies are passed in / read via a callable, so the
 * whole thing is unit-testable with no real files. @implements ADR-003
 * @id KEUR-DISABLED
 */
#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "config/scopes.h"

namespace keur::config {

/// Parse `[rules] disabled = ["ID-1", "ID-2"]` from a keur.toml body into the
/// set of disabled rule ids. Tolerates inline arrays and one-id-per-line.
/// Pure — no I/O.
[[nodiscard]] std::set<std::string> parse_disabled(const std::string& toml_body);

/// Collect the union of disabled ids across every resolved config layer that
/// exists. `read_file` returns a layer's toml body ("" when unreadable).
[[nodiscard]] std::set<std::string> resolve_disabled(
    const std::vector<Layer>& layers,
    const std::function<std::string(const std::string& path)>& read_file);

/// Serialise a disabled-id set back into a `[rules]` section body (one
/// `disabled = [...]` array, ids sorted for a stable diff). Pure.
[[nodiscard]] std::string render_disabled_section(const std::set<std::string>& ids);

/// Return a new toml body with the `[rules] disabled` array set to `ids`,
/// preserving every other section/line. If a `[rules]` section exists its
/// `disabled` line is replaced (or added); otherwise a `[rules]` section is
/// appended. An empty `ids` set removes the disabled line / section. Pure.
[[nodiscard]] std::string upsert_disabled_section(const std::string& toml_body,
                                                  const std::set<std::string>& ids);

}  // namespace keur::config
