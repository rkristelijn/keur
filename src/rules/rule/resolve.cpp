/**
 * @file rule/resolve.cpp
 * @brief ADR-039 difficulty resolution: map (rule, difficulty vector) -> active.
 *
 * This is the ONE mapping the gate and all three difficulty views
 * (level/settings/manager) read (docs/design/difficulty-chooser-ui.md). The
 * scan engine stays dumb (ADR-009/ADR-014): resolution is pure data logic here,
 * never a second scan path.
 *
 * @implements ADR-039
 */
#include "rule/internal.h"

#include <algorithm>

namespace keur {

namespace {
// ADR-039 §1 / §5: derive a single ility from a legacy `category:` when a rule
// declares no explicit `ilities:`. Mirrors scripts/backfill-ilities.sh so
// load-time fallback == the baked data. Returns the PRIMARY ility only. The
// ilities are ISO/IEC 25010 sub-characteristics.
std::string ility_from_category(const std::string& cat) {
  // Security
  if (cat == "secrets" || cat == "secret" || cat == "pii" ||
      cat == "data-exposure" || cat == "cbi")
    return "confidentiality";
  if (cat == "sqli" || cat == "cmdi" || cat == "xss" || cat == "sast" ||
      cat == "security" || cat == "supply-chain" || cat == "iac" || cat == "k8s" ||
      cat == "docker" || cat == "container" || cat == "ansible" || cat == "nginx" ||
      cat == "compliance")
    return "integrity";
  if (cat == "oauth" || cat == "session" || cat == "api" || cat == "transport" ||
      cat == "graphql")
    return "authenticity";
  if (cat == "ai-ml" || cat == "slop") return "modifiability";
  // Reliability
  if (cat == "correctness" || cat == "reliability") return "reliability-maturity";
  if (cat == "resilience") return "fault-tolerance";
  if (cat == "observability" || cat == "logging") return "availability";
  if (cat == "test" || cat == "tests") return "testability";
  // Maintainability
  if (cat == "quality" || cat == "architecture") return "analysability";
  if (cat == "style" || cat == "patterns" || cat == "antipattern" ||
      cat == "best-practice" || cat == "deprecated" || cat == "migration")
    return "modifiability";
  if (cat == "build" || cat == "bundler") return "modularity";
  // Performance Efficiency
  if (cat == "performance" || cat == "perf" || cat == "animation") return "time-behaviour";
  // Usability
  if (cat == "a11y" || cat == "accessibility" || cat == "inclusivity" ||
      cat == "forms" || cat == "ui-frameworks" || cat == "web")
    return "accessibility";
  // Portability
  if (cat == "shell" || cat == "portability") return "adaptability";
  // Traceability (process control, ADR-010)
  if (cat == "docs" || cat == "project" || cat == "git") return "traceability";
  // Default: maintainability analysability.
  return "analysability";
}

// id-prefix override: KEUR-PORT-* are portability rules filed under category
// `architecture` — route to adaptability regardless of category.
std::string ility_override_for_id(const std::string& id) {
  if (id.rfind("KEUR-PORT-", 0) == 0) return "adaptability";
  return "";
}
}  // namespace

int DifficultyVector::level_of(const std::string& ility) const {
  for (const auto& kv : levels)
    if (kv.first == ility) return kv.second;
  return default_level;
}

std::vector<std::string> rule_ilities(const Rule& rule) {
  if (!rule.ilities.empty()) return rule.ilities;
  // Backward-compat (ADR-039 §5): no explicit ilities -> id-override first, else
  // derive from category.
  std::string ov = ility_override_for_id(rule.id);
  if (!ov.empty()) return {ov};
  return {ility_from_category(rule.category)};
}

bool rule_active(const Rule& rule, const DifficultyVector& vec) {
  // Maturity 0 (unset) defaults to 3 (ADR-026).
  const int maturity = rule.maturity > 0 ? rule.maturity : 3;

  // OR/max over the rule's ilities: the STRONGEST non-OFF ility decides.
  int best = 0;  // 0 = no contributing (non-OFF) ility seen yet
  for (const auto& ility : rule_ilities(rule)) {
    const int lvl = vec.level_of(ility);  // 0 = OFF
    if (lvl > best) best = lvl;
  }
  if (best == 0) return false;  // all ilities OFF -> inactive
  return best >= maturity;
}

}  // namespace keur
