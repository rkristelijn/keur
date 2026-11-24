/**
 * @file rule/parse.cpp
 * @brief Parse a .rule file into a Rule; load a directory of rules.
 */
#include "rule/internal.h"

#include <dirent.h>     // keur:ignore KEUR-PORT-002 — engine I/O seam; rules/ is exempt (rule exclude_paths + TODO I/O-via-platform)
#include <sys/stat.h>   // keur:ignore KEUR-PORT-002 — same tracked seam

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace keur {

using detail::ends_with;
using detail::split_ws;
using detail::starts_with;
using detail::trim;

namespace {

// Strip one enclosing pair of matching quotes (single or double).
std::string unquote(const std::string& s) {
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
    return s.substr(1, s.size() - 2);
  return s;
}

// Apply one key:value field to a LiteralClass (ADR-058/059).
void apply_class_field(LiteralClass& lc, const std::string& k, const std::string& v) {
  if (k == "name") lc.name = unquote(v);
  else if (k == "bucket") lc.bucket = unquote(v);
  else if (k == "regex") lc.regex_str = unquote(v);
  else if (k == "threshold") lc.threshold = std::atoi(v.c_str());
  else if (k == "advice") lc.advice = unquote(v);
}

// Parse an inline class row: fields separated by two-or-more spaces, each
// "key: value". `regex`/`advice` may contain single spaces, so we split on the
// 2+-space boundary, not every space. Order of fields is free.
void parse_class_row(const std::string& row, LiteralClass& lc) {
  size_t i = 0, n = row.size();
  while (i < n) {
    size_t key_start = i;
    size_t c = row.find(':', i);
    if (c == std::string::npos) break;
    std::string k = trim(row.substr(key_start, c - key_start));
    // value runs until a 2+-space gap that is followed by a "word:" token.
    size_t v_start = c + 1;
    size_t j = v_start;
    size_t v_end = n;
    while (j < n) {
      if (row[j] == ' ' && j + 1 < n && row[j + 1] == ' ') {
        size_t k2 = j;
        while (k2 < n && row[k2] == ' ') k2++;
        size_t cc = row.find(':', k2);
        size_t sp = row.find(' ', k2);
        if (cc != std::string::npos && (sp == std::string::npos || cc < sp)) {
          v_end = j; break;  // next key starts here
        }
      }
      j++;
    }
    std::string v = trim(row.substr(v_start, v_end - v_start));
    apply_class_field(lc, k, v);
    i = (v_end < n) ? v_end + 1 : n;
    while (i < n && row[i] == ' ') i++;
  }
}

// Parse a stage row: "verb: arg" (e.g. "extract: literals", "count: unique",
// "classify: { table: literal-classes }").
void parse_stage_row(const std::string& row, PipelineStage& st) {
  size_t c = row.find(':');
  if (c == std::string::npos) { st.verb = trim(row); return; }
  st.verb = trim(row.substr(0, c));
  std::string rest = trim(row.substr(c + 1));
  if (!rest.empty() && rest.front() == '{') {
    // inline map, e.g. { table: literal-classes }
    std::string inner = rest.substr(1, rest.size() - (rest.back() == '}' ? 2 : 1));
    size_t ic = inner.find(':');
    if (ic != std::string::npos) {
      std::string k = trim(inner.substr(0, ic)), v = trim(inner.substr(ic + 1));
      if (k == "table") st.table = v;
      else st.arg = v;
    }
  } else {
    st.arg = unquote(rest);
  }
}

// ADR-014 §3: the rule-file schema for scalar fields, as data. One row per key;
// a new field is one new row, the parse loop does not change.
using FieldSetter = void (*)(Rule&, const std::string&);
struct RuleField {
  std::string_view key;
  FieldSetter set;
};

bool is_yes(const std::string& v) { return v == "true" || v == "yes"; }

// ADR-039: inline array `[a, b]` or a bare scalar `security`.
std::vector<std::string> parse_ilities(const std::string& val) {
  std::string v = val;
  if (v.size() >= 2 && v.front() == '[' && v.back() == ']') v = v.substr(1, v.size() - 2);
  for (char& ch : v) if (ch == ',') ch = ' ';
  return split_ws(v);
}

constexpr RuleField kRuleFields[] = {
    {"id", [](Rule& r, const std::string& v) { r.id = v; }},
    {"title", [](Rule& r, const std::string& v) { r.title = v; }},
    {"category", [](Rule& r, const std::string& v) { r.category = v; }},
    {"severity", [](Rule& r, const std::string& v) { r.severity = v; }},
    {"engine", [](Rule& r, const std::string& v) { r.engine = v; }},
    {"action", [](Rule& r, const std::string& v) { r.action = v; }},
    {"confidence", [](Rule& r, const std::string& v) { r.confidence = v; }},
    {"fix", [](Rule& r, const std::string& v) { r.fix = v; }},
    {"source", [](Rule& r, const std::string& v) { r.source = v; }},
    {"message", [](Rule& r, const std::string& v) { r.message = v; }},
    {"block_inner", [](Rule& r, const std::string& v) { r.block_inner = v; }},
    {"block_trigger", [](Rule& r, const std::string& v) { r.block_trigger = v; }},
    {"min_count", [](Rule& r, const std::string& v) { r.min_count = std::atoi(v.c_str()); }},
    {"maturity", [](Rule& r, const std::string& v) { r.maturity = std::atoi(v.c_str()); }},  // @id KEUR-MATURITY.parse — maturity band (ADR-026)
    {"ilities", [](Rule& r, const std::string& v) { r.ilities = parse_ilities(v); }},
    {"ility", [](Rule& r, const std::string& v) { r.ilities = parse_ilities(v); }},
    {"within", [](Rule& r, const std::string& v) { r.within = v; }},
    {"window", [](Rule& r, const std::string& v) { r.window = std::atoi(v.c_str()); }},
    {"path_capture", [](Rule& r, const std::string& v) { r.path_capture = v; }},
    {"derived_template", [](Rule& r, const std::string& v) { r.derived_template = v; }},
    {"extract", [](Rule& r, const std::string& v) { r.extract = v; }},
    {"capture", [](Rule& r, const std::string& v) { r.capture = std::atoi(v.c_str()); }},
    {"case", [](Rule& r, const std::string& v) { r.name_case = v; }},         // filename-case engine
    {"scope", [](Rule& r, const std::string& v) { r.name_scope = v; }},       // filename | dirname
    {"allow", [](Rule& r, const std::string& v) { r.name_allow = split_ws(v); }},  // exempt names
    {"skip_comments", [](Rule& r, const std::string& v) { r.skip_comments = is_yes(v); }},
    {"skip_strings", [](Rule& r, const std::string& v) { r.skip_strings = is_yes(v); }},
    {"skip_emoji", [](Rule& r, const std::string& v) { r.skip_emoji = is_yes(v); }},
    {"skip_binary", [](Rule& r, const std::string& v) { r.skip_binary = is_yes(v); }},
    {"extensions", [](Rule& r, const std::string& v) { r.target.extensions = split_ws(v); }},
    {"filenames", [](Rule& r, const std::string& v) { r.target.filenames = split_ws(v); }},
    {"exclude_paths", [](Rule& r, const std::string& v) { r.target.exclude_paths = split_ws(v); }},
    {"content_contains", [](Rule& r, const std::string& v) { r.target.content_contains = v; }},
    {"generic", [](Rule& r, const std::string& v) { r.generic = is_yes(v); }},  // ADR-059
    {"report", [](Rule& r, const std::string& v) { r.report = v; }},            // ADR-060
};

// Query: the table row for `key`, or nullptr. Linear scan: ~35 rows, called
// once per rule-file line, faster than hashing at this size.
const RuleField* find_rule_field(const std::string& key) {
  for (const auto& f : kRuleFields)
    if (f.key == key) return &f;
  return nullptr;
}

}  // namespace

Rule rule_parse(const std::string& path) {
  Rule rule;
  std::ifstream f(path);
  if (!f.is_open()) return rule;

  std::string line;
  bool in_patterns = false;
  bool in_requires = false;   // ADR-009: inside a requires: block
  bool in_composite = false;  // ADR-009: inside a composite: block
  bool in_classes = false;    // ADR-058/059: inside a classes: block
  bool in_stages = false;     // ADR-060: inside a stages: block
  RulePattern cur;

  while (std::getline(f, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;

    // requires: block — indented sub-keys lang/framework/version (flat form).
    if (in_requires && starts_with(line, "  ") && t.find(':') != std::string::npos) {
      auto c = t.find(':');
      std::string k = trim(t.substr(0, c)), v = trim(t.substr(c + 1));
      if (k == "lang" || k == "langs") rule.requires_.langs = split_ws(v);
      else if (k == "framework" || k == "frameworks") rule.requires_.frameworks = split_ws(v);
      else if (k == "version" || k == "versions") {
        // "name <constraint>" — first token is the package, rest is the predicate.
        auto toks = split_ws(v);
        if (toks.size() >= 2) {
          std::string name = toks[0], constraint;
          for (size_t i = 1; i < toks.size(); i++) { if (i > 1) constraint += " "; constraint += toks[i]; }
          rule.requires_.versions.emplace_back(name, constraint);  // keur:ignore KEUR-PORT-007 — std::pair for version constraints has a constructor — safe
        }
      }
      continue;
    } else if (in_requires) {
      in_requires = false;  // dedented — fall through to top-level parse
    }

    // composite: block — indented all/any/not with space-separated rule refs.
    if (in_composite && starts_with(line, "  ") && t.find(':') != std::string::npos) {
      auto c = t.find(':');
      std::string k = trim(t.substr(0, c)), v = trim(t.substr(c + 1));
      CompositeNode::Kind kind;
      if (k == "all") kind = CompositeNode::All;
      else if (k == "any") kind = CompositeNode::Any;
      else if (k == "not") kind = CompositeNode::Not;
      else if (k == "ref") { rule.composite.kind = CompositeNode::Ref; rule.composite.ref = v; rule.has_composite = true; continue; }
      else continue;
      // The composite's root becomes this operator; children are ref leaves.
      rule.composite.kind = kind;
      rule.composite.children.clear();
      for (const auto& id : split_ws(v)) {
        CompositeNode leaf; leaf.kind = CompositeNode::Ref; leaf.ref = id;
        rule.composite.children.push_back(std::move(leaf));
      }
      rule.has_composite = true;
      continue;
    } else if (in_composite) {
      in_composite = false;
    }

    // classes: block (ADR-058/059) — each "  - name: ..." row is one class.
    // Fields may be on the same row (name/bucket/regex/threshold) with `advice`
    // optionally on its own indented line, OR each field on its own line.
    if (in_classes && starts_with(line, "  - ")) {
      LiteralClass lc;
      // the row after "- " is a sequence of "key: value" separated by two+ spaces
      std::string row = trim(line.substr(line.find("- ") + 2));
      parse_class_row(row, lc);
      rule.classes.push_back(lc);
      continue;
    } else if (in_classes && starts_with(line, "    ") && t.find(':') != std::string::npos) {
      // continuation field for the last class (e.g. advice: on its own line)
      if (!rule.classes.empty()) {
        auto c = t.find(':');
        std::string k = trim(t.substr(0, c)), v = trim(t.substr(c + 1));
        apply_class_field(rule.classes.back(), k, v);
      }
      continue;
    } else if (in_classes && starts_with(line, "  ")) {
      continue;
    } else if (in_classes) {
      in_classes = false;  // dedented — fall through
    }

    // stages: block (ADR-060) — each "  - verb: arg" row is one pipeline stage.
    if (in_stages && starts_with(line, "  - ")) {
      PipelineStage st;
      std::string row = trim(line.substr(line.find("- ") + 2));
      parse_stage_row(row, st);
      rule.stages.push_back(st);
      continue;
    } else if (in_stages && starts_with(line, "    ") && t.find(':') != std::string::npos) {
      if (!rule.stages.empty()) {
        auto c = t.find(':');
        std::string k = trim(t.substr(0, c)), v = trim(t.substr(c + 1));
        if (k == "table") rule.stages.back().table = v;
        else if (k == "arg") rule.stages.back().arg = v;
      }
      continue;
    } else if (in_stages && starts_with(line, "  ")) {
      continue;
    } else if (in_stages) {
      in_stages = false;
    }

    // patterns: sub-items are indented "  - regex:" / "    message:"
    if (in_patterns && starts_with(line, "  - regex:")) {
      if (!cur.regex_str.empty()) { rule.patterns.push_back(std::move(cur)); cur = {}; }
      cur.regex_str = trim(line.substr(line.find("regex:") + 6));
      if (cur.regex_str.size() >= 2 && cur.regex_str.front() == '"' && cur.regex_str.back() == '"')
        cur.regex_str = cur.regex_str.substr(1, cur.regex_str.size() - 2);
      continue;
    } else if (in_patterns && starts_with(line, "    message:")) {
      cur.message = trim(line.substr(line.find("message:") + 8));
      if (cur.message.size() >= 2 && cur.message.front() == '"' && cur.message.back() == '"')
        cur.message = cur.message.substr(1, cur.message.size() - 2);
      continue;
    } else if (in_patterns && starts_with(line, "    exclude:")) {
      cur.exclude_str = trim(line.substr(line.find("exclude:") + 8));
      if (cur.exclude_str.size() >= 2 && cur.exclude_str.front() == '"' && cur.exclude_str.back() == '"')
        cur.exclude_str = cur.exclude_str.substr(1, cur.exclude_str.size() - 2);
      continue;
    } else if (in_patterns && starts_with(line, "  ")) {
      continue;  // other indented sub-key we don't use
    } else if (in_patterns) {
      if (!cur.regex_str.empty()) { rule.patterns.push_back(std::move(cur)); cur = {}; }
      in_patterns = false;
      // fall through to parse this line as a top-level key
    }

    auto colon = t.find(':');
    if (colon == std::string::npos) continue;
    std::string key = trim(t.substr(0, colon));
    std::string val = trim(t.substr(colon + 1));
    // Strip an enclosing pair of identical quotes (single or double). TOML/YAML
    // authors quote values like path_capture: '([a-z]+)/' to protect regex
    // metacharacters; without this the quotes leak into the value. Only a
    // matching outer pair is stripped, so an unquoted value with an inner quote
    // is untouched.
    if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') &&
        val.back() == val.front())
      val = val.substr(1, val.size() - 2);

    // ADR-014 §3: scalar fields dispatch through the kRuleFields table; only the
    // keys that switch parser state stay as a short, closed chain.
    if (const RuleField* f = find_rule_field(key)) f->set(rule, val);
    else if (key == "patterns") { in_patterns = true; cur = {}; }
    else if (key == "requires") { in_requires = true; }
    else if (key == "composite") { in_composite = true; }
    else if (key == "classes") { in_classes = true; }                            // ADR-058/059
    else if (key == "stages") { in_stages = true; }                              // ADR-060
  }
  if (!cur.regex_str.empty()) rule.patterns.push_back(std::move(cur));
  return rule;
}

namespace {
void find_rule_files(const std::string& dir, std::vector<std::string>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    std::string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) find_rule_files(full, out);
    else if (ends_with(name, ".rule")) out.push_back(full);
  }
  closedir(d);
}
}  // namespace

std::vector<Rule> rules_load(const std::string& dir) {
  std::vector<Rule> rules;
  std::vector<std::string> files;
  find_rule_files(dir, files);
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    Rule r = rule_parse(f);
    // pattern-based engines need >=1 pattern; file-level engines (file-absence/
    // file-presence) and composites (they compose other rules) are valid with none.
    // path-derived (ADR-020) computes its expectation from the path, so it needs
    // no regex pattern either — a message is optional (falls back to the title).
    const bool file_level = (r.engine == "file-absence" || r.engine == "file-presence");
    const bool composite = (r.engine == "composite" || r.has_composite);
    const bool path_derived = (r.engine == "path-derived" &&
                               !r.path_capture.empty() && !r.derived_template.empty());
    // filename-case (ADR-naming): a file-level naming-convention check; it needs
    // no regex pattern, just a `case:` style.
    const bool name_case = (r.engine == "filename-case" && !r.name_case.empty());
    // generic rules (ADR-059) carry a data payload (classes/stages), not
    // patterns; the engine=pipeline analyze rule is the first such case.
    const bool generic = r.generic || r.engine == "pipeline";
    if (!r.id.empty() && (!r.patterns.empty() || file_level || composite || path_derived || name_case || generic))
      rules.push_back(std::move(r));
  }
  return rules;
}

}  // namespace keur
