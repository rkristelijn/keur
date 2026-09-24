/**
 * @file rule/composite.cpp
 * @brief ADR-009 requires: guard + composite (all/any/not/ref) evaluator.
 * @implements ADR-009.requires ADR-009.composite
 *
 * The engine stays dumb: this file evaluates DATA (a facts predicate, a boolean
 * tree over rule ids), it holds no framework/CVE/version knowledge. Composites
 * compose the results of ordinary rules that also run standalone; they add no
 * new match primitive — they run AFTER the normal pass, over the ids that fired.
 */
#include "rule/internal.h"
#include "rule/semver.h"

namespace keur::detail {

bool requires_satisfied(const Rule& rule, const Facts& facts) {
  const RuleRequires& req = rule.requires_;
  if (req.empty()) return true;  // no guard => always applies (backwards compatible)

  // langs: any-of. At least one required language must be present.
  if (!req.langs.empty()) {
    bool any = false;
    for (const auto& l : req.langs)
      if (facts.langs.count(l)) { any = true; break; }
    if (!any) return false;
  }

  // frameworks: all-of. Each required framework must be present. Match is a
  // substring on the framework key so "express" satisfies a go.mod path like
  // "github.com/labstack/echo" only when it actually contains the token.
  for (const auto& fw : req.frameworks) {
    bool found = false;
    for (const auto& have : facts.frameworks)
      if (have == fw || have.find(fw) != std::string::npos) { found = true; break; }
    if (!found) return false;
  }

  // versions: each predicate must hold against the DIRECT pin. A missing pin =>
  // not satisfied (ADR-009: silence over a guessed finding).
  for (const auto& [name, constraint] : req.versions) {
    std::string pin;
    // exact key first, then a substring match (go.mod full paths).
    auto it = facts.versions.find(name);
    if (it != facts.versions.end()) {
      pin = it->second;
    } else {
      for (const auto& [k, v] : facts.versions)
        if (k.find(name) != std::string::npos) { pin = v; break; }
    }
    if (pin.empty()) return false;
    if (!semver_satisfies(pin, constraint)) return false;
  }
  return true;
}

bool composite_eval(const CompositeNode& node,
                    const std::unordered_set<std::string>& fired_ids) {
  switch (node.kind) {
    case CompositeNode::Ref:
      return fired_ids.contains(node.ref);
    case CompositeNode::All:
      for (const auto& c : node.children)
        if (!composite_eval(c, fired_ids)) return false;
      return true;
    case CompositeNode::Any:
      for (const auto& c : node.children)
        if (composite_eval(c, fired_ids)) return true;
      return false;
    case CompositeNode::Not:
      // `not` negates its single child (or the AND of children if several).
      for (const auto& c : node.children)
        if (composite_eval(c, fired_ids)) return false;
      return true;
  }
  return false;
}

}  // namespace keur::detail
