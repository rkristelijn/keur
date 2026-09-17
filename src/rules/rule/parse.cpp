/**
 * @file rule/parse.cpp
 * @brief Parse a .rule file into a Rule; load a directory of rules.
 */
#include "rule/internal.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
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
  RulePattern cur;

  while (std::getline(f, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;

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
    else if (key == "skip_comments") rule.skip_comments = (val == "true" || val == "yes");
    else if (key == "skip_strings") rule.skip_strings = (val == "true" || val == "yes");
    else if (key == "extensions") rule.target.extensions = split_ws(val);
    else if (key == "filenames") rule.target.filenames = split_ws(val);
    else if (key == "exclude_paths") rule.target.exclude_paths = split_ws(val);
    else if (key == "content_contains") rule.target.content_contains = val;
    else if (key == "patterns") { in_patterns = true; cur = {}; }
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
    // file-presence) are valid with none — they match on filenames/extensions.
    const bool file_level = (r.engine == "file-absence" || r.engine == "file-presence");
    if (!r.id.empty() && (!r.patterns.empty() || file_level)) rules.push_back(std::move(r));
  }
  return rules;
}

}  // namespace keur
