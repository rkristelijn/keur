/**
 * @file rule_engine.h
 * @brief KEUR native rule engine — loads .rule files, evaluates patterns.
 * @see docs/adr/ADR-001-native-rule-engine.md
 * @implements ADR-001
 *
 * Clean-room implementation following cpm's proven design: RE2-powered,
 * single-pass, extension-indexed. Emits KEUR normalised Findings.
 *
 * Supported engines: pattern, presence, absence (≈99% of the rule corpus).
 * Unsupported engines are reported, never silently skipped.
 */
#ifndef KEUR_RULE_ENGINE_H
#define KEUR_RULE_ENGINE_H

#include <re2/re2.h>

#include <memory>
#include <string>
#include <vector>

namespace keur {

/** A single regex pattern within a rule. */
struct RulePattern {
  std::string regex_str;
  std::string message;
  // Optional RE2-friendly "pattern-not": when this second regex matches the
  // same line, the match is suppressed. Lets a rule express "match X except
  // when Y" without PCRE lookahead — keeps the engine single, RE2, linear.
  // @see docs/adr/ADR-007-pattern-exclude.md
  // @implements ADR-007
  std::string exclude_str;
};

/** Which files a rule applies to. */
struct RuleTarget {
  std::vector<std::string> extensions;     // {".py", ".js"}
  std::vector<std::string> filenames;      // {"Dockerfile"}
  std::vector<std::string> exclude_paths;  // {"test/", "vendor/"}
  std::string content_contains;            // literal fast pre-filter
};

/**
 * A `requires:` guard (ADR-009): facts predicate that gates whether a rule is
 * loaded/run at all. Empty guard = always applies (backwards compatible).
 * @implements ADR-009.requires
 */
struct RuleRequires {
  std::vector<std::string> langs;       // any-of: at least one must be present
  std::vector<std::string> frameworks;  // all-of: each must be present
  // version predicates: package/module name -> constraint (e.g. "<4.18.0").
  std::vector<std::pair<std::string, std::string>> versions;
  bool empty() const { return langs.empty() && frameworks.empty() && versions.empty(); }
};

/**
 * A composite boolean node (ADR-009): combines the results of OTHER rules.
 * A node is either a leaf (`ref` to a rule id) or an operator over children.
 * @implements ADR-009.composite
 */
struct CompositeNode {
  enum Kind { Ref, All, Any, Not } kind = Ref;
  std::string ref;                    // for Kind::Ref — the rule id it points at
  std::vector<CompositeNode> children;  // for All/Any/Not
};

/** A complete rule loaded from a .rule file. */
struct Rule {
  std::string id;
  std::string title;
  std::string category;
  std::string severity;    // "error"|"warning"|"info"|"critical"
  std::string engine;      // "pattern"|"presence"|"absence"
  std::string action;      // optional: "block"|"warn"|"advisory"
  std::string confidence;  // optional: "0.0".."1.0"
  std::string fix;
  std::string message;     // optional: default finding message (fallback for patterns without their own)
  std::string block_inner;    // block-absence: regex that MUST appear inside the block
  std::string block_trigger;  // block-absence: optional gate — only check blocks containing this
  int min_count = 0;          // count engine: fire only at >= this many matches (ADR-014)
  std::string within;         // count engine: scope — "file" (default). Reserved for "block".
  int window = 0;             // multiline engine: sliding window size in lines (default 2)
  bool skip_comments = false;  // strip comments before matching (ADR-004 stage 1)
  bool skip_strings = false;   // strip string literals before matching
  RuleRequires requires_;      // ADR-009: facts guard (empty = always applies)
  CompositeNode composite;     // ADR-009: for engine=composite, the boolean tree
  bool has_composite = false;  // true when engine=composite and a tree was parsed
  RuleTarget target;
  std::vector<RulePattern> patterns;
};

/** A finding produced by the engine (mapped to KEUR Finding JSON on output). */
struct RuleFinding {
  std::string rule_id;
  std::string title;
  std::string category;
  std::string severity;
  std::string action;
  std::string confidence;
  std::string file;
  int line = 0;
  std::string message;
};

/** Parse a single .rule file. */
Rule rule_parse(const std::string& path);

/** Load all .rule files under a directory (recursively), sorted by path. */
std::vector<Rule> rules_load(const std::string& dir);

/** True if a file's relative path matches a rule's target spec. */
bool rule_matches_file(const Rule& rule, const std::string& rel_path);

/** Scan an explicit list of files (used by the hook — the staged diff). */
std::vector<RuleFinding> rules_scan_files(const std::vector<Rule>& rules,
                                          const std::vector<std::string>& files);

/** Scan a whole project directory (used by `keur scan`). */
std::vector<RuleFinding> rules_scan_dir(const std::vector<Rule>& rules,
                                        const std::string& root);

}  // namespace keur

#endif  // KEUR_RULE_ENGINE_H
