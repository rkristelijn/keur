/**
 * @file rule/parse.cpp
 * @brief Parse a .rule file into a Rule; load a directory of rules.
 */
#include "rule/internal.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace keur {

using detail::ends_with;
using detail::split_ws;
using detail::starts_with;
using detail::trim;

Rule rule_parse(const std::string& path) {
  Rule rule;
  std::ifstream f(path);
  if (!f.is_open()) return rule;

  std::string line;
  bool in_patterns = false;
  bool in_requires = false;   // ADR-009: inside a requires: block
  bool in_composite = false;  // ADR-009: inside a composite: block
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
          rule.requires_.versions.emplace_back(name, constraint);
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
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);

    if (key == "id") rule.id = val;
    else if (key == "title") rule.title = val;
    else if (key == "category") rule.category = val;
    else if (key == "severity") rule.severity = val;
    else if (key == "engine") rule.engine = val;
    else if (key == "action") rule.action = val;
    else if (key == "confidence") rule.confidence = val;
    else if (key == "fix") rule.fix = val;
    else if (key == "message") rule.message = val;
    else if (key == "block_inner") rule.block_inner = val;
    else if (key == "block_trigger") rule.block_trigger = val;
    else if (key == "min_count") rule.min_count = std::atoi(val.c_str());
    else if (key == "within") rule.within = val;
    else if (key == "window") rule.window = std::atoi(val.c_str());
    else if (key == "skip_comments") rule.skip_comments = (val == "true" || val == "yes");
    else if (key == "skip_strings") rule.skip_strings = (val == "true" || val == "yes");
    else if (key == "extensions") rule.target.extensions = split_ws(val);
    else if (key == "filenames") rule.target.filenames = split_ws(val);
    else if (key == "exclude_paths") rule.target.exclude_paths = split_ws(val);
    else if (key == "content_contains") rule.target.content_contains = val;
    else if (key == "patterns") { in_patterns = true; cur = {}; }
    else if (key == "requires") { in_requires = true; }
    else if (key == "composite") { in_composite = true; }
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
    const bool file_level = (r.engine == "file-absence" || r.engine == "file-presence");
    const bool composite = (r.engine == "composite" || r.has_composite);
    if (!r.id.empty() && (!r.patterns.empty() || file_level || composite)) rules.push_back(std::move(r));
  }
  return rules;
}

}  // namespace keur
