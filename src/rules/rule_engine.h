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

/**
 * A rule's action — the single source of truth for the gate verbs. Historically
 * these were bare strings (`"block"`, `"warn"`, …) compared ad-hoc across ~18
 * files, which drifts: a typo like `"wrn"` compiles and fails silently. The
 * enum + the central from/to-string pair below make a typo in code a compile
 * error and a typo in rule data a validation failure (ADR-058 drift class).
 * The string stays in the Rule/Finding structs (serialisation unchanged); this
 * is the conversion/validation layer around it, not a struct change.
 */
enum class Action { Block, Warn, Advisory, Off, Unknown };

/** Parse an action string to the enum. Unknown/empty → Action::Unknown. */
Action action_from_string(const std::string& s);

/** Canonical lowercase string for an action (round-trips with the parser). */
std::string action_to_string(Action a);

/** True if `s` is one of the known action verbs (block|warn|advisory|off). */
bool action_is_valid(const std::string& s);

/**
 * A rule's severity — the companion drift-safe enum to Action. Historically
 * compared ad-hoc as strings (`severity == "error"`), same drift risk. Order
 * reflects escalation (Info < Warning < Error < Critical). Unknown/empty →
 * Severity::Unknown. The string stays in the struct; this is the conversion
 * layer around it.
 */
enum class Severity { Info, Warning, Error, Critical, Unknown };

/** Parse a severity string to the enum. Unknown/empty → Severity::Unknown. */
Severity severity_from_string(const std::string& s);

/** Canonical lowercase string for a severity (round-trips with the parser). */
std::string severity_to_string(Severity s);

/** True if `s` is one of the known severities (info|warning|error|critical). */
bool severity_is_valid(const std::string& s);

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

/**
 * One row of a literal-classification table (ADR-058/059). A generic rule
 * carries an ordered list of these; the first whose `regex` matches a literal
 * wins. `bucket` is noise|magic|identity; `threshold` is the rule-of-three
 * minimum occurrence count for the magic bucket (0 = not applicable).
 */
struct LiteralClass {
  std::string name;       // e.g. "EMAIL", "LONG_NUM", "WORD"
  std::string bucket;     // "noise" | "magic" | "identity"
  std::string regex_str;  // match pattern (first match wins, in list order)
  int threshold = 0;      // magic bucket: min occurrences to flag (rule of three)
  std::string advice;     // human guidance when this class is surfaced
};

/**
 * One stage of a declarative pipeline (ADR-060): a fixed verb plus its params.
 * The closed verb set is implemented in C++; data only sequences them.
 */
struct PipelineStage {
  std::string verb;   // "extract" | "count" | "classify"
  std::string arg;    // e.g. "literals" (extract), "unique" (count)
  std::string table;  // classify: the generic rule id holding the class table
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
  std::string source;      // optional provenance: e.g. "cpm" for rules adopted
                           // from cpm. Metadata only — the engine ignores it;
                           // it records origin (attribution, upstream-sync,
                           // corpus-exclude) so imported rules need no separate
                           // directory. Empty = keur's own rule.
  std::string message;     // optional: default finding message (fallback for patterns without their own)
  std::string block_inner;    // block-absence: regex that MUST appear inside the block
  std::string block_trigger;  // block-absence: optional gate — only check blocks containing this
  int min_count = 0;          // count engine: fire only at >= this many matches (ADR-014)
  // Maturity band (ADR-026): the LOWEST project level (1..5) at which this rule
  // gates. keur activates a rule when its maturity <= the project's chosen level
  // ([policy] level in keur.toml, set by `keur level`). 0 = unset -> defaults to
  // 3 (defined) at filter time. Metadata only; the engine ignores it.
  int maturity = 0;
  // Ilities (ADR-039): the quality dimension(s) this rule belongs to, used by the
  // difficulty vector to decide whether the rule is active. First = primary (UI
  // home + average); rest = secondary. Resolution is OR/max over the vector (a
  // rule is active when the STRONGEST of its ilities' levels >= its maturity), so
  // lowering a secondary ility can never silently disable a dangerous rule.
  // Empty -> the resolver falls back to mapping `category` to an ility. Metadata
  // only; the scan engine ignores it (ADR-009). @implements ADR-039
  std::vector<std::string> ilities;
  std::string within;         // count engine: scope — "file" (default). Reserved for "block".
  int window = 0;             // multiline engine: sliding window size in lines (default 2)
  // path-derived engine (ADR-020): the expected string is computed from the
  // file's path. `path_capture` is a regex with capture group(s) applied to the
  // relative path; `derived_template` is the expected string with \1, \2, …
  // placeholders substituted from those groups. If the path matches and the
  // derived string is absent from the file, the rule fires. @implements ADR-020
  std::string path_capture;
  std::string derived_template;
  // extract-duplicates engine: `extract` is a regex with a capture group; the
  // captured value (group `capture`, default 1) is collected per file and the
  // rule fires on the SECOND+ occurrence of the same value. @implements ADR-004.stage2
  std::string extract;
  int capture = 1;
  // filename-case engine (ADR-naming): enforce a naming convention on a file's
  // basename (scope=filename) or each path dir segment (scope=dirname). `name_case`
  // is one of upper-snake|lower-snake|kebab|pascal|camel|upper|lower. `name_allow`
  // lists exact basenames exempt from the check (e.g. README, LICENSE, TODO).
  std::string name_case;                 // the required case style (empty = engine off)
  std::string name_scope = "filename";   // filename | dirname
  std::vector<std::string> name_allow;   // exempt exact names
  bool skip_comments = false;  // strip comments before matching (ADR-004 stage 1)
  bool skip_strings = false;   // strip string literals before matching
  bool skip_binary = false;    // opt-out: skip per-line matching on binary files
                               // (NUL-byte content). Default false = scan all,
                               // so a rule that must inspect binaries is unchanged;
                               // a rule prone to binary false positives (PII/secret
                               // regexes on PDFs/images) sets this. @id KEUR-BINARY-SKIP
  RuleRequires requires_;      // ADR-009: facts guard (empty = always applies)
  CompositeNode composite;     // ADR-009: for engine=composite, the boolean tree
  bool has_composite = false;  // true when engine=composite and a tree was parsed
  // Generic rule (ADR-059): a rule that feeds an engine instead of firing on its
  // own. `generic` makes it inert to standalone dispatch; the consuming engine
  // reads its payload by id. `classes` is the literal-classify table; `stages`
  // is the declarative pipeline (ADR-060); `report` names the output shape.
  bool generic = false;
  std::vector<LiteralClass> classes;   // ADR-058/059: classification table
  std::vector<PipelineStage> stages;   // ADR-060: ordered pipeline verbs
  std::string report;                  // ADR-060: e.g. "by-bucket"
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
  std::string fix;  // remediation hint from the rule's fix: field, carried to output

  RuleFinding() = default;

  // Explicit constructor so vector::emplace_back works on every toolchain.
  // libc++ (older Xcode on the macOS CI runner) cannot emplace an aggregate;
  // libstdc++ tolerates it. Providing a constructor keeps both green.
  // @see KEUR-PORT-007-emplace-aggregate
  RuleFinding(std::string rule_id_, std::string title_, std::string category_,
              std::string severity_, std::string action_, std::string confidence_,
              std::string file_, int line_, std::string message_, std::string fix_ = {})
      : rule_id(std::move(rule_id_)),
        title(std::move(title_)),
        category(std::move(category_)),
        severity(std::move(severity_)),
        action(std::move(action_)),
        confidence(std::move(confidence_)),
        file(std::move(file_)),
        line(line_),
        message(std::move(message_)),
        fix(std::move(fix_)) {}
};

/**
 * One classified literal — the output row of the literal-analyze pipeline
 * (ADR-058/060). `count` is how often the literal occurs across the scanned
 * fileset; `klass`/`bucket`/`advice` come from the matched class-table row.
 * `file`/`line` point at the first occurrence (for the human to go look).
 */
struct ClassifiedLiteral {
  std::string text;
  int count = 0;
  std::string klass;   // e.g. "EMAIL", "WORD" — empty if no class matched
  std::string bucket;  // "noise" | "magic" | "identity" | "" (unclassified)
  std::string advice;
  std::string file;    // first occurrence
  int line = 0;        // first occurrence (1-based)
};

/**
 * Run the literal-analyze pipeline (ADR-060) over a set of files, using a
 * generic classify rule (ADR-059) that carries the class table. Executes the
 * fixed verbs extract -> count -> classify. Returns one row per unique literal.
 * Noise-bucket literals are kept in the result (the caller decides to drop
 * them) so the function stays a pure transform. Read-only; never blocks.
 */
std::vector<ClassifiedLiteral> run_literal_pipeline(const Rule& classify_rule,
                                                    const std::vector<std::string>& files);

/** Parse a single .rule file. */
Rule rule_parse(const std::string& path);

/** Load all .rule files under a directory (recursively), sorted by path. */
std::vector<Rule> rules_load(const std::string& dir);

/** True if a file's relative path matches a rule's target spec. */
bool rule_matches_file(const Rule& rule, const std::string& rel_path);

/** Scan an explicit list of files (used by the hook — the staged diff). */
std::vector<RuleFinding> rules_scan_files(const std::vector<Rule>& rules,
                                          const std::vector<std::string>& files);

/**
 * Scan staged files for per-line rules, but evaluate FILE-LEVEL rules
 * (file-absence/file-presence/max-dir-files/filename-case) over the whole repo
 * tree rooted at `repo_root`. This is what the hook wants: a file-absence rule
 * ("project has no SECURITY.md") is a repo property, not a diff property, so it
 * must not fire just because SECURITY.md is absent from THIS commit. Per-line
 * rules still run only over `files`, keeping the hook sub-second.
 * If `repo_root` is empty, behaves exactly like `rules_scan_files`.
 */
std::vector<RuleFinding> rules_scan_files_scoped(const std::vector<Rule>& rules,
                                                 const std::vector<std::string>& files,
                                                 const std::string& repo_root);

/** Scan a whole project directory (used by `keur scan`). */
std::vector<RuleFinding> rules_scan_dir(const std::vector<Rule>& rules,
                                        const std::string& root);

// --- ADR-039: difficulty resolution (which rules are active) ----------------

/**
 * A difficulty vector: a maturity level (1..5) per ility, or OFF.
 * This is the canonical state the three views (level/settings/manager) all
 * write and the gate reads (docs/design/difficulty-chooser-ui.md).
 * @implements ADR-039
 */
struct DifficultyVector {
  // ility name -> level. Level 0 means OFF (dimension not gated). A level is
  // 1..5 (ADR-026). An ility absent from the map falls back to `default_level`.
  std::vector<std::pair<std::string, int>> levels;
  int default_level = 1;  // ADR-026/ADR-011: fresh keur starts at CMMI-1.

  // Level for an ility: explicit if set, else default_level. 0 = OFF.
  int level_of(const std::string& ility) const;
};

/**
 * The ilities that steer a rule: its explicit `ilities`, or — when empty — a
 * single ility derived from its `category` (ADR-039 §5 backward-compat).
 */
std::vector<std::string> rule_ilities(const Rule& rule);

/**
 * ADR-039 §3 resolution: a rule is ACTIVE when the strongest (max) of its
 * ilities' levels in the vector meets the rule's maturity bar. An OFF ility
 * (level 0) does not contribute; if all its ilities are OFF the rule is
 * inactive. Maturity 0 is treated as the ADR-026 default of 3.
 */
bool rule_active(const Rule& rule, const DifficultyVector& vec);

}  // namespace keur

#endif  // KEUR_RULE_ENGINE_H
