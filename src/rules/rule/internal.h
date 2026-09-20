/**
 * @file rule/internal.h
 * @brief Shared internals for the split rule engine (parse/match/scan).
 *
 * Not a public interface (rule_engine.h is). These small helpers and the
 * compiled-rule type are shared across the engine's translation units so each
 * .cpp can stay focused on one responsibility.
 */
#pragma once

#include <re2/re2.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "rule_engine.h"
#include "rule/facts.h"

namespace keur::detail {

// --- small string helpers ---
std::string trim(const std::string& s);
std::vector<std::string> split_ws(const std::string& s);
bool ends_with(const std::string& s, const std::string& suf);
bool starts_with(const std::string& s, const std::string& pre);
std::string get_ext(const std::string& p);
std::string basename_of(const std::string& p);

// --- finding defaults (severity -> action/confidence) ---
std::string default_action(const Rule& r);
std::string default_conf(const Rule& r);

// Message fallback chain for a finding: a pattern's own message, else the
// rule-level `message:`, else the rule title.
std::string finding_msg(const Rule& r, const std::string& pattern_message);

// --- line splitting (RE2 StringPieces over the content buffer) ---
void split_lines(const std::string& content, std::vector<re2::StringPiece>& lines);

// --- inline keur:ignore suppression ---
bool line_suppresses(std::string_view line, const std::string& id, const std::string& cat);

// --- a rule with its patterns pre-compiled to RE2 ---
struct Compiled {
  const Rule* rule = nullptr;
  std::vector<std::unique_ptr<RE2>> res;
  // Parallel to `res`: optional pattern-not regex per pattern (nullptr = none).
  // When set and it matches the same line, the pattern's match is suppressed.
  std::vector<std::unique_ptr<RE2>> exclude_res;
  std::unique_ptr<RE2> block_inner;    // block-absence: required inner regex
  std::unique_ptr<RE2> block_trigger;  // block-absence: optional gate regex
};

// block-absence engine (rule/block.cpp): flag a brace-scoped block that opens,
// (optionally) contains the trigger, but never contains the inner regex.
void eval_block_absence(const Rule& rule, const RE2* start_re, const RE2* inner_re,
                        const RE2* trigger_re, const std::vector<re2::StringPiece>& lines,
                        const std::string& rel, std::vector<RuleFinding>& findings);

// --- engine strategy registry (ADR-014) -------------------------------------
// Per-line engines (pattern/presence/absence/block-absence/count/...) share this
// context: the compiled rule, the lines to scan, the file path. Each engine is a
// function registered by name; scan.cpp looks it up instead of an if/else chain.
// File-level engines (file-absence/file-presence) run once over the fileset in
// eval_file_level_rules and are NOT in this registry.
struct ScanContext {
  const Compiled& compiled;                       // rule + pre-compiled regexes
  const std::vector<re2::StringPiece>& lines;     // raw or comment/string-stripped
  const std::string& rel;                         // file path (relative)
};

using EngineFn = void (*)(const ScanContext&, std::vector<RuleFinding>&);

// Look up an engine by name; nullptr if unknown (caller warns once).
EngineFn engine_lookup(const std::string& name);

// --- ADR-009: requires: guard + composite evaluator -------------------------

// True if a rule's requires: guard is satisfied by the derived facts. An empty
// guard is always satisfied (backwards compatible). @implements ADR-009.requires
bool requires_satisfied(const Rule& rule, const Facts& facts);

// Evaluate a composite boolean tree over the set of rule-ids that produced at
// least one finding this scan. Returns true if the composite should fire.
// Leaves (ref) are true when the referenced rule fired. @implements ADR-009.composite
bool composite_eval(const CompositeNode& node,
                    const std::unordered_set<std::string>& fired_ids);

}  // namespace keur::detail
