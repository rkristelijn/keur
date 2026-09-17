/**
 * @file cmd/rules.cpp
 * @brief keur rules list — the resolved rule cascade as a tree.
 */
#include "cmd/rules.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "util/keur_utils.h"

namespace keur::cmd {

namespace {

// A layer name for a resolved rule directory, inferred from its path, plus a
// specificity rank (higher = wins the cascade), mirroring CSS: built-in is the
// user-agent stylesheet (weakest), the explicit override is an inline style
// (strongest).
struct Layer {
  std::string dir;
  std::string name;
  int specificity;   // 1..4
};

Layer classify(const std::string& dir, int order_index) {
  // Heuristic from the path; falls back to the resolution order.
  if (dir.find("/.keur/rules") != std::string::npos)
    return {dir, "project  (.keur/rules)", 3};
  if (dir.find("/keur/rules") != std::string::npos &&
      (dir.find("/.config") != std::string::npos ||
       dir.find("XDG") != std::string::npos))
    return {dir, "global   (~/.config/keur/rules)", 2};
  // KEUR_HOME/rules is the built-in set; everything else that is neither
  // project nor global is treated as an explicit override.
  if (dir.find("/rules") != std::string::npos && order_index == 0)
    return {dir, "built-in (KEUR_HOME/rules)", 1};
  return {dir, "override (KEUR_RULES_DIRS)", 4};
}

const char* spec_bar(int s) {
  switch (s) {
    case 1: return "▁";
    case 2: return "▃";
    case 3: return "▆";
    default: return "█";
  }
}

}  // namespace

RulesOptions rules_parse(const std::vector<std::string>& args) {
  RulesOptions o;
  for (const auto& a : args) {
    if (a == "list") o.subcommand = "list";
    else if (a == "--winners") o.winners_only = true;
  }
  return o;
}

int rules(const RulesOptions& opts) {
  auto dirs = rule_dirs();
  if (dirs.empty()) {
    std::cerr << "keur rules: no rule directories resolved "
                 "(set KEUR_HOME or add .keur/rules)\n";
    return 0;
  }

  // Load each layer and track, per rule id, which layer currently wins (the
  // highest specificity seen). This is the cascade: a later, more specific
  // layer overrides an earlier one with the same id.
  struct Winner { int specificity; std::string layer; };
  std::map<std::string, Winner> winner;

  struct LoadedLayer { Layer layer; std::vector<Rule> rules; };
  std::vector<LoadedLayer> layers;

  int idx = 0;
  for (const auto& d : dirs) {
    Layer L = classify(d, idx++);
    auto rs = rules_load(d);
    for (const auto& r : rs) {
      auto it = winner.find(r.id);
      if (it == winner.end() || L.specificity >= it->second.specificity)
        winner[r.id] = {L.specificity, L.name};
    }
    layers.push_back({L, std::move(rs)});
  }

  std::cout << "keur rule cascade  (specificity: built-in ▁ < global ▃ < "
               "project ▆ < override █; higher wins)\n\n";

  int total = 0, shadowed = 0;
  for (const auto& LL : layers) {
    std::cout << spec_bar(LL.layer.specificity) << " " << LL.layer.name
              << "   [" << LL.layer.dir << "]\n";
    if (LL.rules.empty()) {
      std::cout << "  (no rules)\n\n";
      continue;
    }
    // group by category for a readable tree
    std::map<std::string, std::vector<const Rule*>> by_cat;
    for (const auto& r : LL.rules) by_cat[r.category].push_back(&r);

    for (const auto& [cat, rs] : by_cat) {
      std::cout << "  " << (cat.empty() ? "(uncategorised)" : cat) << "\n";
      for (const auto* r : rs) {
        ++total;
        bool wins = winner[r->id].specificity == LL.layer.specificity &&
                    winner[r->id].layer == LL.layer.name;
        const char* mark = wins ? "✓" : "·";   // · = shadowed by a stronger layer
        if (!wins) ++shadowed;
        if (opts.winners_only && !wins) continue;
        std::cout << "    " << mark << " " << r->id;
        if (!r->severity.empty()) std::cout << "  [" << r->severity << "]";
        if (!r->title.empty())    std::cout << "  " << r->title;
        if (!wins)
          std::cout << "   (shadowed by " << winner[r->id].layer << ")";
        std::cout << "\n";
      }
    }
    std::cout << "\n";
  }

  std::cout << total << " rule(s) across " << layers.size() << " layer(s)";
  if (shadowed) std::cout << "; " << shadowed << " shadowed by a more specific layer";
  std::cout << ".\n";
  return 0;
}

}  // namespace keur::cmd
