/**
 * @file rule_engine.cpp
 * @brief KEUR native rule engine — parser + RE2-powered single-pass scanner.
 * @see docs/adr/ADR-001-native-rule-engine.md
 *
 * Performance model (mirrors cpm): walk the tree once, read each file once,
 * dispatch rules by extension/filename, pre-filter by a literal substring,
 * pre-compile RE2 patterns. RE2 is linear-time (no ReDoS).
 */
#include "rule_engine.h"

#include "tokenizer.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace keur {

// ── small string helpers ─────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
static std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string p;
  while (iss >> p) out.push_back(p);
  return out;
}
static bool ends_with(const std::string& s, const std::string& suf) {
  return suf.size() <= s.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static bool starts_with(const std::string& s, const std::string& pre) {
  return s.compare(0, pre.size(), pre) == 0;
}
static std::string get_ext(const std::string& p) {
  auto dot = p.rfind('.');
  return dot == std::string::npos ? "" : p.substr(dot);
}
static std::string basename_of(const std::string& p) {
  auto slash = p.rfind('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}

// ── parser ───────────────────────────────────────────────────────────────────
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
      // strip optional surrounding quotes
      if (cur.regex_str.size() >= 2 && cur.regex_str.front() == '"' && cur.regex_str.back() == '"')
        cur.regex_str = cur.regex_str.substr(1, cur.regex_str.size() - 2);
      continue;
    } else if (in_patterns && starts_with(line, "    message:")) {
      cur.message = trim(line.substr(line.find("message:") + 8));
      if (cur.message.size() >= 2 && cur.message.front() == '"' && cur.message.back() == '"')
        cur.message = cur.message.substr(1, cur.message.size() - 2);
      continue;
    } else if (in_patterns && starts_with(line, "  ")) {
      continue;  // some other indented sub-key we don't use
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

// ── loader ───────────────────────────────────────────────────────────────────
static void find_rule_files(const std::string& dir, std::vector<std::string>& out) {
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

std::vector<Rule> rules_load(const std::string& dir) {
  std::vector<Rule> rules;
  std::vector<std::string> files;
  find_rule_files(dir, files);
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    Rule r = rule_parse(f);
    // pattern-based engines need at least one pattern; file-level engines
    // (file-absence/file-presence) are valid with no patterns — they match on
    // the target's filenames/extensions across the whole fileset.
    const bool file_level = (r.engine == "file-absence" || r.engine == "file-presence");
    if (!r.id.empty() && (!r.patterns.empty() || file_level)) rules.push_back(std::move(r));
  }
  return rules;
}

// ── file matching ────────────────────────────────────────────────────────────
bool rule_matches_file(const Rule& rule, const std::string& rel) {
  bool has_ext = !rule.target.extensions.empty();
  bool has_name = !rule.target.filenames.empty();
  if (has_ext || has_name) {
    bool match = false;
    if (has_ext) {
      std::string ext = get_ext(rel);
      for (auto& e : rule.target.extensions)
        if (ext == e || ends_with(rel, e)) { match = true; break; }
    }
    if (!match && has_name) {
      std::string b = basename_of(rel);
      for (auto& fn : rule.target.filenames)
        if (b == fn) { match = true; break; }
    }
    if (!match) return false;
  }
  for (auto& ex : rule.target.exclude_paths)
    if (rel.find(ex) != std::string::npos) return false;
  return true;
}

// ── suppression: keur:ignore [<id|category>] (token-exact) ───────────────────
static bool tokens_match(const std::string& rest, const std::string& id, const std::string& cat) {
  std::istringstream iss(rest);
  std::string tok;
  while (iss >> tok) {
    // trim trailing separators
    while (!tok.empty() && (tok.back() == ',' )) tok.pop_back();
    if (!id.empty() && tok == id) return true;
    if (!cat.empty() && tok == cat) return true;
  }
  return false;
}
static bool line_suppresses(std::string_view line, const std::string& id, const std::string& cat) {
  auto pos = line.find("keur:ignore");
  if (pos == std::string_view::npos) return false;
  std::string rest(line.substr(pos + 11));
  auto s = rest.find_first_not_of(" \t:,");
  if (s == std::string::npos) return true;  // bare keur:ignore suppresses all
  return tokens_match(rest.substr(s), id, cat);
}

// ── compiled form ────────────────────────────────────────────────────────────
struct Compiled {
  const Rule* rule = nullptr;
  std::vector<std::unique_ptr<RE2>> res;
};

static std::string default_action(const Rule& r) {
  if (!r.action.empty()) return r.action;
  if (r.severity == "error" || r.severity == "critical") return "block";
  if (r.severity == "warning") return "warn";
  return "advisory";
}
static std::string default_conf(const Rule& r) {
  if (!r.confidence.empty()) return r.confidence;
  std::string a = default_action(r);
  if (a == "block") return "0.9";
  if (a == "warn") return "0.6";
  return "0.4";
}

static void split_lines(const std::string& content, std::vector<re2::StringPiece>& lines) {
  const char* p = content.data();
  const char* end = p + content.size();
  const char* ls = p;
  while (p <= end) {
    if (p == end || *p == '\n') { lines.emplace_back(ls, p - ls); ls = p + 1; }
    p++;
  }
}

// ── file-level engines (ADR-004 stage 2) ────────────────────────────────────
// file-absence: emit a finding when NONE of the target's filenames/extensions
//   is present in the scanned fileset (e.g. "missing SECURITY.md").
// file-presence: emit a finding for EACH matching file (e.g. "committed .env").
// These run once over the whole fileset, not per line. Adopted from cpm
// (adr-166); shaped to keur's RuleFinding.
static void eval_file_level_rules(
    const std::vector<const Rule*>& absence_rules,
    const std::vector<const Rule*>& presence_rules,
    const std::vector<std::pair<std::string, std::string>>& files,
    std::vector<RuleFinding>& findings) {
  std::unordered_set<std::string> basenames;
  for (const auto& [rel, abs] : files) basenames.insert(basename_of(rel));

  for (const auto* rule : absence_rules) {
    bool found = false;
    for (const auto& fn : rule->target.filenames) {
      if (basenames.count(fn)) { found = true; break; }
    }
    if (!found) {
      for (const auto& [rel, abs] : files) {
        if (rule_matches_file(*rule, rel)) { found = true; break; }
      }
    }
    if (!found) {
      std::string expected = rule->target.filenames.empty()
          ? (rule->target.extensions.empty() ? "matching file"
                                             : rule->target.extensions[0])
          : rule->target.filenames[0];
      findings.push_back({rule->id, rule->title, rule->category, rule->severity,
                          default_action(*rule), default_conf(*rule), expected, 0,
                          rule->title});
    }
  }

  for (const auto* rule : presence_rules) {
    for (const auto& [rel, abs] : files) {
      if (rule_matches_file(*rule, rel)) {
        findings.push_back({rule->id, rule->title, rule->category, rule->severity,
                            default_action(*rule), default_conf(*rule), rel, 0,
                            rule->title});
      }
    }
  }
}

// ── core scan of a set of (rel_path, absolute_path) files ────────────────────
static std::vector<RuleFinding> scan_impl(const std::vector<Rule>& rules,
                                          const std::vector<std::pair<std::string, std::string>>& files) {
  std::vector<RuleFinding> findings;

  // extension / filename index for dispatch; rules with no target run on all.
  std::unordered_map<std::string, std::vector<const Rule*>> ext_index, name_index;
  std::vector<const Rule*> all_rules;
  for (auto& r : rules) {
    if (r.target.extensions.empty() && r.target.filenames.empty()) all_rules.push_back(&r);
    else {
      for (auto& e : r.target.extensions) ext_index[e].push_back(&r);
      for (auto& fn : r.target.filenames) name_index[fn].push_back(&r);
    }
  }

  // pre-compile RE2 for each rule
  std::vector<Compiled> compiled;
  compiled.reserve(rules.size());
  std::unordered_map<const Rule*, size_t> to_compiled;
  for (auto& r : rules) {
    Compiled c;
    c.rule = &r;
    RE2::Options opt;
    opt.set_log_errors(false);
    for (auto& p : r.patterns) {
      auto re = std::make_unique<RE2>(p.regex_str, opt);
      if (!re->ok()) {
        std::cerr << "keur: invalid regex in rule " << r.id << ": " << re->error() << "\n";
        c.res.push_back(nullptr);
      } else {
        c.res.push_back(std::move(re));
      }
    }
    to_compiled[&r] = compiled.size();
    compiled.push_back(std::move(c));
  }

  std::unordered_map<std::string, std::string> id_to_cat;
  for (auto& r : rules) id_to_cat[r.id] = r.category;

  for (auto& [rel, abs] : files) {
    std::string ext = get_ext(rel), base = basename_of(rel);

    std::vector<const Rule*> candidates = all_rules;
    if (auto it = ext_index.find(ext); it != ext_index.end())
      candidates.insert(candidates.end(), it->second.begin(), it->second.end());
    for (auto& [key, rv] : ext_index)  // compound ext (.test.js)
      if (key != ext && key.size() > ext.size() && ends_with(rel, key))
        candidates.insert(candidates.end(), rv.begin(), rv.end());
    if (auto it = name_index.find(base); it != name_index.end())
      candidates.insert(candidates.end(), it->second.begin(), it->second.end());
    if (candidates.empty()) continue;

    // exclude_paths filter
    std::vector<const Rule*> applicable;
    for (auto* r : candidates) {
      bool excl = false;
      for (auto& e : r->target.exclude_paths)
        if (rel.find(e) != std::string::npos) { excl = true; break; }
      if (!excl) applicable.push_back(r);
    }
    if (applicable.empty()) continue;

    std::ifstream f(abs);
    if (!f.is_open()) continue;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // content_contains literal pre-filter
    std::vector<const Rule*> filtered;
    for (auto* r : applicable)
      if (r->target.content_contains.empty() || content.find(r->target.content_contains) != std::string::npos)
        filtered.push_back(r);
    if (filtered.empty()) continue;

    std::vector<re2::StringPiece> raw_lines;
    split_lines(content, raw_lines);

    // Comment/string-aware matching (ADR-004 stage 1): if any applicable rule
    // asks to skip comments/strings, compute a stripped copy of the file ONCE
    // and match those rules against the stripped lines. Stripping replaces
    // comment/string chars with spaces but preserves newlines, so line numbers
    // stay correct. Rules that do not opt in keep matching the raw lines.
    bool need_comments = false, need_strings = false;
    for (auto* r : filtered) {
      if (r->skip_comments) need_comments = true;
      if (r->skip_strings) need_strings = true;
    }
    std::string stripped_content;
    std::vector<re2::StringPiece> stripped_lines;
    if (need_comments || need_strings) {
      const LangSyntax* syntax = lang_syntax(get_ext(rel));
      if (syntax) {
        stripped_content = need_strings ? strip_comments_and_strings(content, syntax)
                                        : strip_comments(content, syntax);
        split_lines(stripped_content, stripped_lines);
      }
    }

    size_t before = findings.size();
    for (auto* r : filtered) {
      auto& c = compiled[to_compiled[r]];
      const std::string engine = r->engine.empty() ? "pattern" : r->engine;
      // Match against stripped lines for opt-in rules (only when stripping
      // actually ran — i.e. the language was recognised); else the raw lines.
      const std::vector<re2::StringPiece>& lines =
          ((r->skip_comments || r->skip_strings) && !stripped_lines.empty())
              ? stripped_lines
              : raw_lines;

      if (engine == "pattern") {
        for (size_t pi = 0; pi < c.res.size(); pi++) {
          if (!c.res[pi]) continue;
          for (size_t li = 0; li < lines.size(); li++)
            if (RE2::PartialMatch(lines[li], *c.res[pi])) {
              const std::string& msg = r->patterns[pi].message.empty() ? r->title : r->patterns[pi].message;
              findings.push_back({r->id, r->title, r->category, r->severity, default_action(*r),
                                  default_conf(*r), rel, (int)(li + 1), msg});
            }
        }
      } else if (engine == "presence") {
        for (size_t pi = 0; pi < c.res.size(); pi++) {
          if (!c.res[pi]) continue;
          for (size_t li = 0; li < lines.size(); li++)
            if (RE2::PartialMatch(lines[li], *c.res[pi])) {
              const std::string& msg = r->patterns[pi].message.empty() ? r->title : r->patterns[pi].message;
              findings.push_back({r->id, r->title, r->category, r->severity, default_action(*r),
                                  default_conf(*r), rel, (int)(li + 1), msg});
              break;
            }
        }
      } else if (engine == "absence") {
        for (size_t pi = 0; pi < c.res.size(); pi++) {
          if (!c.res[pi]) continue;
          bool found = false;
          for (size_t li = 0; li < lines.size() && !found; li++)
            if (RE2::PartialMatch(lines[li], *c.res[pi])) found = true;
          if (!found) {
            const std::string& msg = r->patterns[pi].message.empty() ? r->title : r->patterns[pi].message;
            findings.push_back({r->id, r->title, r->category, r->severity, default_action(*r),
                                default_conf(*r), rel, 1, msg});
          }
        }
      } else {
        static std::unordered_set<std::string> warned;
        if (warned.insert(engine).second)
          std::cerr << "keur: unsupported engine '" << engine << "' (rule " << r->id << ")\n";
      }
    }

    // inline keur:ignore suppression (same-line for line findings)
    if (findings.size() > before) {
      std::vector<RuleFinding> kept;
      for (size_t i = before; i < findings.size(); i++) {
        const auto& fnd = findings[i];
        bool suppressed = false;
        if (fnd.line >= 1 && (size_t)fnd.line <= raw_lines.size()) {
          std::string_view lv(raw_lines[fnd.line - 1].data(), raw_lines[fnd.line - 1].size());
          suppressed = line_suppresses(lv, fnd.rule_id, id_to_cat[fnd.rule_id]);
        }
        if (!suppressed) kept.push_back(fnd);
      }
      findings.resize(before);
      for (auto& k : kept) findings.push_back(std::move(k));
    }
  }

  // file-level engines run once over the whole fileset (not per line).
  std::vector<const Rule*> file_absence, file_presence;
  for (const auto& r : rules) {
    if (r.engine == "file-absence") file_absence.push_back(&r);
    else if (r.engine == "file-presence") file_presence.push_back(&r);
  }
  if (!file_absence.empty() || !file_presence.empty()) {
    eval_file_level_rules(file_absence, file_presence, files, findings);
  }

  return findings;
}

std::vector<RuleFinding> rules_scan_files(const std::vector<Rule>& rules,
                                          const std::vector<std::string>& files) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (auto& f : files) pairs.emplace_back(f, f);  // rel == abs for explicit list
  return scan_impl(rules, pairs);
}

// dir walk with the same skip list as cpm (build output, deps, caches)
static void walk(const std::string& dir, const std::string& prefix,
                 std::vector<std::pair<std::string, std::string>>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    std::string rel = prefix.empty() ? name : prefix + "/" + name;
    std::string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    // Hidden DIRECTORIES are skipped (.git, .cache, ...) except a few CI dirs;
    // but hidden FILES are kept — .env / .npmrc / .dockercfg are exactly the
    // secret-bearing files a scanner must see (needed for file-presence rules).
    if (name[0] == '.' && S_ISDIR(st.st_mode) &&
        name != ".github" && name != ".gitlab" && name != ".ci")
      continue;
    if (S_ISDIR(st.st_mode) &&
        (name == "node_modules" || name == "vendor" || name == "build" || name == "dist" ||
         name == "out" || name == "target" || name == "coverage" || name == "__pycache__" ||
         name == "venv" || name == "env" || name == "bin" || name == "obj" || name == "tmp"))
      continue;
    if (S_ISDIR(st.st_mode)) walk(full, rel, out);
    else if (S_ISREG(st.st_mode) && st.st_size < 1024 * 1024) out.emplace_back(rel, full);
  }
  closedir(d);
}

std::vector<RuleFinding> rules_scan_dir(const std::vector<Rule>& rules, const std::string& root) {
  std::vector<std::pair<std::string, std::string>> files;
  walk(root, "", files);
  std::sort(files.begin(), files.end());
  return scan_impl(rules, files);
}

}  // namespace keur
