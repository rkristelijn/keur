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
#include <vector>

namespace keur::cmd {

struct RulesOptions {
  std::string subcommand = "list";   // only "list" for now
  bool winners_only = false;          // --winners: show only the effective rule per id
};

RulesOptions rules_parse(const std::vector<std::string>& args);
int rules(const RulesOptions& opts);

}  // namespace keur::cmd
