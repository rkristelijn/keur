/**
 * @file rule/scan.cpp
 * @brief The single-pass scanner: dispatch, pre-filter, match, suppress.
 *
 * Performance model (mirrors cpm): walk once, read each file once, dispatch by
 * extension/filename, literal content pre-filter, pre-compiled RE2. Engines:
 * pattern / presence / absence (per line) + file-absence / file-presence
 * (once over the fileset).
 */
#include "rule/internal.h"
#include "tokenizer.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace keur {

using namespace detail;

namespace {

// file-absence: flag when NONE of the target's filenames/extensions exist in
// the fileset. file-presence: flag EACH matching file. Once over the fileset.
void eval_file_level_rules(
    const std::vector<const Rule*>& absence_rules,
    const std::vector<const Rule*>& presence_rules,
    const std::vector<std::pair<std::string, std::string>>& files,
    std::vector<RuleFinding>& findings) {
  std::unordered_set<std::string> basenames;
  for (const auto& [rel, abs] : files) basenames.insert(basename_of(rel));

  for (const auto* rule : absence_rules) {
    bool found = false;
    for (const auto& fn : rule->target.filenames)
      if (basenames.count(fn)) { found = true; break; }
    if (!found)
      for (const auto& [rel, abs] : files)
        if (rule_matches_file(*rule, rel)) { found = true; break; }
    if (!found) {
      std::string expected = rule->target.filenames.empty()
          ? (rule->target.extensions.empty() ? "matching file" : rule->target.extensions[0])
          : rule->target.filenames[0];
      findings.push_back({rule->id, rule->title, rule->category, rule->severity,
                          default_action(*rule), default_conf(*rule), expected, 0, finding_msg(*rule, "")});
    }
  }
  for (const auto* rule : presence_rules)
    for (const auto& [rel, abs] : files)
      if (rule_matches_file(*rule, rel))
        findings.push_back({rule->id, rule->title, rule->category, rule->severity,
                            default_action(*rule), default_conf(*rule), rel, 0, finding_msg(*rule, "")});
}

std::vector<RuleFinding> scan_impl(const std::vector<Rule>& rules,
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
    // block-absence: compile the inner + optional trigger regexes.
    if (!r.block_inner.empty()) {
      auto bi = std::make_unique<RE2>(r.block_inner, opt);
      if (bi->ok()) c.block_inner = std::move(bi);
      else std::cerr << "keur: invalid block_inner in rule " << r.id << ": " << bi->error() << "\n";
    }
    if (!r.block_trigger.empty()) {
      auto bt = std::make_unique<RE2>(r.block_trigger, opt);
      if (bt->ok()) c.block_trigger = std::move(bt);
      else std::cerr << "keur: invalid block_trigger in rule " << r.id << ": " << bt->error() << "\n";
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

    std::vector<const Rule*> filtered;
    for (auto* r : applicable)
      if (r->target.content_contains.empty() || content.find(r->target.content_contains) != std::string::npos)
        filtered.push_back(r);
    if (filtered.empty()) continue;

    std::vector<re2::StringPiece> raw_lines;
    split_lines(content, raw_lines);

    // Comment/string-aware matching (ADR-004 stage 1): strip once per file when
    // an applicable rule opts in; match those rules against the stripped lines.
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
      const std::vector<re2::StringPiece>& lines =
          ((r->skip_comments || r->skip_strings) && !stripped_lines.empty()) ? stripped_lines : raw_lines;

      if (engine == "pattern") {
        for (size_t pi = 0; pi < c.res.size(); pi++) {
          if (!c.res[pi]) continue;
          for (size_t li = 0; li < lines.size(); li++)
            if (RE2::PartialMatch(lines[li], *c.res[pi])) {
              const std::string msg = finding_msg(*r, r->patterns[pi].message);
              findings.push_back({r->id, r->title, r->category, r->severity, default_action(*r),
                                  default_conf(*r), rel, (int)(li + 1), msg});
            }
        }
      } else if (engine == "presence") {
        for (size_t pi = 0; pi < c.res.size(); pi++) {
          if (!c.res[pi]) continue;
          for (size_t li = 0; li < lines.size(); li++)
            if (RE2::PartialMatch(lines[li], *c.res[pi])) {
              const std::string msg = finding_msg(*r, r->patterns[pi].message);
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
            const std::string msg = finding_msg(*r, r->patterns[pi].message);
            findings.push_back({r->id, r->title, r->category, r->severity, default_action(*r),
                                default_conf(*r), rel, 1, msg});
          }
        }
      } else if (engine == "block-absence") {
        eval_block_absence(*r, c.res.empty() ? nullptr : c.res[0].get(),
                           c.block_inner.get(), c.block_trigger.get(), lines, rel, findings);
      } else if (engine != "file-absence" && engine != "file-presence") {
        static std::unordered_set<std::string> warned;
        if (warned.insert(engine).second)
          std::cerr << "keur: unsupported engine '" << engine << "' (rule " << r->id << ")\n";
      }
    }

    // inline keur:ignore suppression (checked against the RAW line)
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

  // file-level engines run once over the whole fileset.
  std::vector<const Rule*> file_absence, file_presence;
  for (const auto& r : rules) {
    if (r.engine == "file-absence") file_absence.push_back(&r);
    else if (r.engine == "file-presence") file_presence.push_back(&r);
  }
  if (!file_absence.empty() || !file_presence.empty())
    eval_file_level_rules(file_absence, file_presence, files, findings);

  return findings;
}

// dir walk: skip hidden DIRS (.git) + build/deps/caches; keep hidden FILES
// (.env, .npmrc) so file-presence can see secret-bearing dotfiles.
void walk(const std::string& dir, const std::string& prefix,
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

}  // namespace

std::vector<RuleFinding> rules_scan_files(const std::vector<Rule>& rules,
                                          const std::vector<std::string>& files) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (auto& f : files) pairs.emplace_back(f, f);  // rel == abs for an explicit list
  return scan_impl(rules, pairs);
}

std::vector<RuleFinding> rules_scan_dir(const std::vector<Rule>& rules, const std::string& root) {
  std::vector<std::pair<std::string, std::string>> files;
  walk(root, "", files);
  std::sort(files.begin(), files.end());
  return scan_impl(rules, files);
}

}  // namespace keur
