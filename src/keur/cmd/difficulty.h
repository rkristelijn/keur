/**
 * @file cmd/difficulty.h
 * @brief `keur difficulty` — explain the resolved difficulty vector and which
 *        rules it activates, and answer why a given rule is (in)active and what
 *        to change to flip it.
 *
 * The verification instrument for the difficulty model (DESIGN-DIFFRES §12): it
 * reads the same resolution the gate uses (merge_difficulty + rule_active), so
 * what it prints is exactly what runs. @implements DESIGN-DIFFRES.explain
 */
#pragma once
#include <string>
#include <vector>

namespace keur::cmd {

struct DifficultyOptions {
  bool        explain = false;   // --explain (default when no other action)
  bool        json    = false;   // --json
  std::string why;               // --why <RULE-ID>
  std::string why_not;           // --why-not <RULE-ID>
};

/** Parse flags from argv. @implements DESIGN-DIFFRES.explain.cmd */
DifficultyOptions difficulty_parse(const std::vector<std::string>& args);

/** Run the difficulty subcommand. Returns 0 on success. */
int difficulty(const DifficultyOptions& opts);

}  // namespace keur::cmd
