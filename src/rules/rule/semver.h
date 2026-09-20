/**
 * @file rule/semver.h
 * @brief Tiny version comparator for `requires:` predicates (ADR-009).
 * @implements ADR-009.requires
 *
 * NOT a solver and NOT SCA (ADR-009 scope limit): it compares a DIRECT pin
 * string against a predicate like "<4.18.0" / ">=1.2" / "1.0.0 - 2.0.0". Just
 * enough to answer "does the pinned version satisfy this constraint?". Missing
 * or unparseable versions => not satisfied (silence over a guessed finding).
 */
#ifndef KEUR_RULE_SEMVER_H
#define KEUR_RULE_SEMVER_H

#include <string>

namespace keur {

/**
 * True if `version` satisfies `constraint`.
 * Supported constraints:
 *   "<X" "<=X" ">X" ">=X" "=X" "X"      single comparison (= / bare = equality)
 *   "A - B"                              inclusive range
 * Versions compared numerically component-by-component (1.9 < 1.10). Non-numeric
 * suffixes (e.g. -alpha) are ignored for the compare. Empty version => false.
 */
bool semver_satisfies(const std::string& version, const std::string& constraint);

/** -1 / 0 / +1 comparing two dotted numeric versions (missing parts = 0). */
int semver_compare(const std::string& a, const std::string& b);

}  // namespace keur

#endif  // KEUR_RULE_SEMVER_H
