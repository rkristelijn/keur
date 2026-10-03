/**
 * @file cmd/learn.h
 * @brief keur learn — teach the USER (or an AI) about a rule (ADR-036).
 *
 *   keur learn <RULE-ID>         print the rule's explainer markdown
 *   keur learn <RULE-ID> --fetch allow a one-off download of the explainer from
 *                                the keur repo if it is not local/cached
 *   keur learn                   DEPRECATED alias for `keur train` (the loop)
 *
 * Where `keur train` trains keur's rule set, `keur learn <RULE>` serves the
 * human/AI that hit a finding: it resolves the explainer markdown offline-first
 *   1. local  $KEUR_HOME/docs/rules/<ID>.md   (shipped with the repo/binary)
 *   2. cache  $XDG_CACHE_HOME/keur/rules/<ID>.md   (previously fetched)
 *   3. remote (opt-in) the keur repo raw URL — ONLY with --fetch or
 *      KEUR_LEARN_FETCH=1 — then cached. Never automatic, never uploads.
 * If no explainer exists, it falls back to the rule's own title/message/fix/
 * category/CWE from the .rule file, so `keur learn <RULE>` is always useful.
 *
 * The no-argument form stays a deprecated alias to `keur train` for one release
 * so running loops (supervise.sh / nightly `keur learn`) keep working.
 */
#pragma once

#include <string>
#include <vector>

namespace keur::cmd {

// --- pure helpers (unit-testable, no I/O) ---------------------------------

/// True if `s` looks like a keur rule id: KEUR-<SEG>-<NNN> style — upper-case
/// letters/digits and single hyphens, no path separators or dots (so it can
/// never escape the docs/rules/ dir when used to build a path). Positive check.
[[nodiscard]] bool is_valid_rule_id(const std::string& s);

/// The explainer path for a rule id relative to a docs/rules base dir:
/// `<base>/<ID>.md`. Assumes `id` already passed is_valid_rule_id.
[[nodiscard]] std::string explainer_path(const std::string& base_dir,
                                         const std::string& id);

// --- command --------------------------------------------------------------

/// Parse + run `keur learn`. With a <RULE-ID> positional, print its explainer;
/// with no rule-id, emit a deprecation note and forward to `keur train`.
int learn(const std::vector<std::string>& args);

}  // namespace keur::cmd
