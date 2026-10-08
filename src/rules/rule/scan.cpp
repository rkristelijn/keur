/**
 * @file rule/scan.cpp
 * @brief The single-pass scanner: dispatch, pre-filter, match, suppress.
 *
 * Performance model (mirrors cpm): walk once, read each file once, dispatch by
 * extension/filename, literal content pre-filter, pre-compiled RE2. Engines:
 * pattern / presence / absence (per line) + file-absence / file-presence
 * (once over the fileset).
 * @implements ADR-001 ADR-004.stage0
 */
#include "rule/internal.h"
#include "tokenizer.h"

#include <dirent.h>     // keur:ignore KEUR-PORT-002 — engine I/O seam; rules/ is exempt (see rule exclude_paths + TODO I/O-via-platform)
#include <sys/stat.h>   // keur:ignore KEUR-PORT-002 — same tracked seam

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace keur {

using namespace detail;

namespace {

// name_matches_case: does `name` conform to the required case `style`? Pure, no
// I/O — the testable core of the filename-case engine (ADR-naming). Styles:
//   lower        all lowercase, no separators      (page, index)
//   upper         all uppercase, no separators      (README-less; rare)
//   lower-snake   lowercase words joined by _       (my_module)
//   upper-snake   uppercase words joined by _       (MY_CONST, CODE_OF_CONDUCT)
//   kebab         lowercase words joined by -       (my-component, lucky13)
//   pascal        each word capitalised, no sep      (MyComponent)
//   camel         pascal but first word lowercase    (myComponent)
// Digits are allowed in every style. An empty name never conforms.
bool name_matches_case(const std::string& name, const std::string& style) {
  if (name.empty()) return false;
  auto is_lower = [](char c) { return (c >= 'a' && c <= 'z'); };
  auto is_upper = [](char c) { return (c >= 'A' && c <= 'Z'); };
  auto is_digit = [](char c) { return (c >= '0' && c <= '9'); };
  auto alnum_lower = [&](char c) { return is_lower(c) || is_digit(c); };
  auto alnum_upper = [&](char c) { return is_upper(c) || is_digit(c); };

  if (style == "lower") {
    for (char c : name) if (!alnum_lower(c)) return false;
    return true;
  }
  if (style == "upper") {
    for (char c : name) if (!alnum_upper(c)) return false;
    return true;
  }
  if (style == "lower-snake" || style == "upper-snake" || style == "kebab") {
    const char sep = (style == "kebab") ? '-' : '_';
    bool upper = (style == "upper-snake");
    bool prev_sep = true;  // leading separator not allowed
    for (char c : name) {
      if (c == sep) { if (prev_sep) return false; prev_sep = true; continue; }
      bool ok = upper ? alnum_upper(c) : alnum_lower(c);
      if (!ok) return false;
      prev_sep = false;
    }
    return !prev_sep;  // no trailing separator
  }
  if (style == "pascal" || style == "camel") {
    // No separators; must be alphanumeric; first letter case fixed by style.
    size_t i = 0;
    // find first alphabetic char
    while (i < name.size() && is_digit(name[i])) i++;
    if (i >= name.size()) return false;  // all digits is not Pascal/camel
    if (style == "pascal" && !is_upper(name[i])) return false;
    if (style == "camel" && !is_lower(name[i])) return false;
    for (char c : name)
      if (!(is_lower(c) || is_upper(c) || is_digit(c))) return false;  // no _ or -
    return true;
  }
  return true;  // unknown style → do not flag (fail open)
}

// filename-case: file-level naming-convention engine (ADR-naming). For each file
// matching the rule's target, check the basename (scope=filename) or each path
// dir segment (scope=dirname) against `name_case`, skipping any name in
// `name_allow`. The basename is checked WITHOUT its extension. @id engine-filename-case
void eval_filename_case(const std::vector<const Rule*>& rules,
                        const std::vector<std::pair<std::string, std::string>>& files,
                        std::vector<RuleFinding>& findings) {
  for (const auto* rule : rules) {
    const std::string& style = rule->name_case;
    const bool dir_scope = (rule->name_scope == "dirname");
    std::unordered_set<std::string> allow(rule->name_allow.begin(), rule->name_allow.end());
    std::unordered_set<std::string> flagged_dirs;  // dir-scope: report each dir once

    for (const auto& [rel, abs] : files) {
      if (!rule_matches_file(*rule, rel)) continue;

      if (!dir_scope) {
        // basename without extension
        std::string base = basename_of(rel);
        std::string stem = base;
        if (auto dot = stem.find_last_of('.'); dot != std::string::npos && dot > 0)
          stem = stem.substr(0, dot);
        if (allow.count(stem) || allow.count(base)) continue;
        if (!name_matches_case(stem, style))
          findings.emplace_back(rule->id, rule->title, rule->category, rule->severity,
                              default_action(*rule), default_conf(*rule), rel, 0,
                              finding_msg(*rule, rule->message));
      } else {
        // each directory segment of the path (not the final basename)
        std::string dir = rel;
        if (auto slash = dir.find_last_of('/'); slash != std::string::npos) dir = dir.substr(0, slash);
        else continue;  // no directory part
        size_t start = 0;
        while (start <= dir.size()) {
          size_t slash = dir.find('/', start);
          std::string seg = dir.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
          if (!seg.empty() && !allow.count(seg) && !name_matches_case(seg, style)) {
            std::string key = dir.substr(0, slash == std::string::npos ? dir.size() : slash);
            if (flagged_dirs.insert(key).second)
              findings.emplace_back(rule->id, rule->title, rule->category, rule->severity,
                                  default_action(*rule), default_conf(*rule), key, 0,
                                  finding_msg(*rule, rule->message));
          }
          if (slash == std::string::npos) break;
          start = slash + 1;
        }
      }
    }
  }
}

// file-absence: flag when NONE of the target's filenames/extensions exist in
// the fileset. file-presence: flag EACH matching file. Once over the fileset.
// These are file-level (not per-line) so they run here, not via the per-line
// engine registry (ADR-014).  @id engine-file-absence  @id engine-file-presence
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
      findings.emplace_back(rule->id, rule->title, rule->category, rule->severity,
                          default_action(*rule), default_conf(*rule), expected, 0, finding_msg(*rule, ""));
    }
  }
  for (const auto* rule : presence_rules)
    for (const auto& [rel, abs] : files)
      if (rule_matches_file(*rule, rel))
        findings.emplace_back(rule->id, rule->title, rule->category, rule->severity,
                            default_action(*rule), default_conf(*rule), rel, 0, finding_msg(*rule, ""));
}

// max-dir-files: FILE-LEVEL engine — flag a directory holding more logical units
// than min_count. A logical unit is a source stem: `a.h` + `a.cpp` count as ONE
// (a header/impl pair is one module). Runs once over the whole fileset, grouping
// by each file's parent directory. This is the cross-file aggregation that a
// per-line engine cannot do, but the file-level layer (which already sees the
// whole fileset, like file-absence) can. Only files matching the rule's target
// (extensions/filenames) are counted, so a rule can scope it to source dirs.
// @id engine-max-dir-files
void eval_max_dir_files(const std::vector<const Rule*>& rules,
                        const std::vector<std::pair<std::string, std::string>>& files,
                        std::vector<RuleFinding>& findings) {
  for (const auto* rule : rules) {
    const int limit = rule->min_count > 0 ? rule->min_count : 10;
    // dir -> set of logical unit stems (path with any source extension removed).
    std::unordered_map<std::string, std::unordered_set<std::string>> units;
    for (const auto& [rel, abs] : files) {
      if (!rule_matches_file(*rule, rel)) continue;
      // Split into directory + filename.
      std::string dir, name = rel;
      if (auto slash = rel.find_last_of("/\\"); slash != std::string::npos) {
        dir = rel.substr(0, slash);
        name = rel.substr(slash + 1);
      }
      // Strip a trailing source extension so a.h and a.cpp share one stem.
      std::string stem = name;
      for (const char* ext : {".hpp", ".cpp", ".cc", ".h", ".c"})
        if (ends_with(stem, ext)) { stem.resize(stem.size() - std::string(ext).size()); break; }
      units[dir].insert(stem);
    }
    for (const auto& [dir, stems] : units)
      if (static_cast<int>(stems.size()) > limit)
        findings.emplace_back(rule->id, rule->title, rule->category, rule->severity,
                            default_action(*rule), default_conf(*rule), dir, 0,
                            finding_msg(*rule, rule->patterns.empty() ? std::string()
                                                                      : rule->patterns[0].message));
  }
}

std::vector<RuleFinding> scan_impl(const std::vector<Rule>& rules,
                                   const std::vector<std::pair<std::string, std::string>>& files,
                                   const Facts& facts,
                                   const std::vector<std::pair<std::string, std::string>>* file_level_files = nullptr) {
  std::vector<RuleFinding> findings;
  // File-level rules (file-absence/presence, max-dir-files, filename-case) ask a
  // REPO question ("does the project have a SECURITY.md?"), not a diff question.
  // In the hook, `files` is only the staged diff, so a file-absence rule would
  // wrongly fire for a file that exists in the repo but is not in this commit.
  // When a caller passes `file_level_files` (the full repo fileset), the
  // file-level pass runs over THAT; the per-line pass still runs over `files`
  // (the staged diff) to keep the hook sub-second. Default: same fileset.
  // @id ADR-052.scope @implements ADR-052
  const std::vector<std::pair<std::string, std::string>>& flfiles =
      file_level_files ? *file_level_files : files;

  // ADR-009: composites run after the single-rule pass; hold them aside. Single
  // rules whose requires: guard isn't satisfied by facts are pruned up front
  // (most-discriminating-first) — they are never indexed, compiled, or run.
  std::vector<const Rule*> composite_rules;
  std::vector<const Rule*> active;
  for (auto& r : rules) {
    if (r.engine == "composite" || r.has_composite) { composite_rules.push_back(&r); continue; }
    if (!requires_satisfied(r, facts)) continue;  // guard failed → skip entirely
    active.push_back(&r);
  }

  // extension / filename index for dispatch; rules with no target run on all.
  std::unordered_map<std::string, std::vector<const Rule*>> ext_index, name_index;
  std::vector<const Rule*> all_rules;
  for (auto* rp : active) {
    const Rule& r = *rp;
    if (r.target.extensions.empty() && r.target.filenames.empty()) all_rules.push_back(&r);
    else {
      for (auto& e : r.target.extensions) ext_index[e].push_back(&r);
      for (auto& fn : r.target.filenames) name_index[fn].push_back(&r);
    }
  }

  // pre-compile RE2 for each ACTIVE rule (pruned rules are never compiled).
  std::vector<Compiled> compiled;
  compiled.reserve(active.size());
  std::unordered_map<const Rule*, size_t> to_compiled;
  for (auto* rp : active) {
    const Rule& r = *rp;
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
      // optional pattern-not (exclude): compiled parallel to res.
      if (p.exclude_str.empty()) {
        c.exclude_res.push_back(nullptr);
      } else {
        auto ex = std::make_unique<RE2>(p.exclude_str, opt);
        if (!ex->ok()) {
          std::cerr << "keur: invalid exclude in rule " << r.id << ": " << ex->error() << "\n";
          c.exclude_res.push_back(nullptr);
        } else {
          c.exclude_res.push_back(std::move(ex));
        }
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
  for (auto& r : rules) id_to_cat[r.id] = r.category;   // all rules, incl. composites

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
    //
    // Two independent strip variants, chosen PER RULE by its own flags. A single
    // shared "widest strip" is wrong: if one rule wants skip_strings and another
    // only skip_comments, a shared comments+strings strip would blank out the
    // string literals that the skip_comments-only rule needs to match (its
    // pattern often targets text inside a string, e.g. a \uXXXX escape). So we
    // keep comments-only and comments+strings separate.
    bool need_comments = false, need_strings = false;
    for (auto* r : filtered) {
      if (r->skip_comments && !r->skip_strings) need_comments = true;
      if (r->skip_strings) need_strings = true;
    }
    std::string comments_only_content, comments_strings_content;
    std::vector<re2::StringPiece> comments_only_lines, comments_strings_lines;
    if (need_comments || need_strings) {
      const LangSyntax* syntax = lang_syntax(get_ext(rel));
      if (syntax) {
        if (need_comments) {
          comments_only_content = strip_comments(content, syntax);
          split_lines(comments_only_content, comments_only_lines);
        }
        if (need_strings) {
          comments_strings_content = strip_comments_and_strings(content, syntax);
          split_lines(comments_strings_content, comments_strings_lines);
        }
      }
    }

    size_t before = findings.size();
    for (auto* r : filtered) {
      auto& c = compiled[to_compiled[r]];
      const std::string engine = r->engine.empty() ? "pattern" : r->engine;
      // Pick the strip variant that matches THIS rule's flags: strings-stripping
      // implies comments too; comments-only keeps string literals intact.
      const std::vector<re2::StringPiece>& lines =
          (r->skip_strings && !comments_strings_lines.empty())      ? comments_strings_lines
          : (r->skip_comments && !comments_only_lines.empty())      ? comments_only_lines
                                                                    : raw_lines;

      // Engine dispatch via the strategy registry (ADR-014): look up the engine
      // by name and run it. File-level engines run separately over the fileset,
      // so they are not in the registry and are skipped here without a warning.
      if (EngineFn fn = engine_lookup(engine)) {
        ScanContext cx{c, lines, rel};
        fn(cx, findings);
      } else if (engine != "file-absence" && engine != "file-presence" &&
                 engine != "max-dir-files" && engine != "filename-case") {
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

  // file-level engines run once over the whole fileset (respect requires: too).
  std::vector<const Rule*> file_absence, file_presence, max_dir_files, filename_case;
  for (const auto& r : rules) {
    if (!requires_satisfied(r, facts)) continue;
    if (r.engine == "file-absence") file_absence.push_back(&r);
    else if (r.engine == "file-presence") file_presence.push_back(&r);
    else if (r.engine == "max-dir-files") max_dir_files.push_back(&r);
    else if (r.engine == "filename-case") filename_case.push_back(&r);
  }
  if (!file_absence.empty() || !file_presence.empty())
    eval_file_level_rules(file_absence, file_presence, flfiles, findings);
  if (!max_dir_files.empty())
    eval_max_dir_files(max_dir_files, flfiles, findings);
  if (!filename_case.empty())
    eval_filename_case(filename_case, flfiles, findings);

  // ADR-009: composite pass. After the single-rule pass, collect the ids that
  // fired, then evaluate each composite's boolean tree over that set. A composite
  // whose requires: guard fails is skipped; one that fires emits a single finding.
  if (!composite_rules.empty()) {
    std::unordered_set<std::string> fired;
    for (const auto& fnd : findings) fired.insert(fnd.rule_id);
    for (const Rule* rp : composite_rules) {
      const Rule& r = *rp;
      if (!requires_satisfied(r, facts)) continue;
      if (!r.has_composite) continue;
      if (composite_eval(r.composite, fired)) {
        findings.emplace_back(r.id, r.title, r.category, r.severity,
                            default_action(r), default_conf(r),
                            r.target.filenames.empty() ? std::string(".") : r.target.filenames[0],
                            0, finding_msg(r, ""), r.fix);
      }
    }
  }

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
    // keur's OWN gitignored generated data (eval/corpus/ clones + eval/history/
    // logs) — huge and not code. This is a keur-specific interim skip; the proper
    // fix (respect .gitignore / move under a dot-dir) is tracked in TODO.md.
    if (S_ISDIR(st.st_mode) && (rel == "eval/corpus" || rel == "eval/history"))
      continue;
    if (S_ISDIR(st.st_mode)) walk(full, rel, out);
    else if (S_ISREG(st.st_mode) && st.st_size < 1024 * 1024) out.emplace_back(rel, full);  // keur:ignore KEUR-PORT-007 — std::pair<string,string> has a constructor — safe
  }
  closedir(d);
}

}  // namespace

std::vector<RuleFinding> rules_scan_files(const std::vector<Rule>& rules,
                                          const std::vector<std::string>& files) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (auto& f : files) pairs.emplace_back(f, f);  // rel == abs for an explicit list  // keur:ignore KEUR-PORT-007 — std::pair<string,string> has a constructor — safe
  // Derive facts from the parent directory of the first file (best-effort for an
  // explicit list; the hook passes staged paths under the repo root).
  std::string root = ".";
  if (!files.empty()) {
    size_t slash = files[0].find_last_of('/');
    if (slash != std::string::npos) root = files[0].substr(0, slash);
  }
  Facts facts = derive_facts(root);
  return scan_impl(rules, pairs, facts);
}

std::vector<RuleFinding> rules_scan_files_scoped(const std::vector<Rule>& rules,
                                                 const std::vector<std::string>& files,
                                                 const std::string& repo_root) {
  // Per-line fileset: the explicit (staged) list, rel == abs.
  std::vector<std::pair<std::string, std::string>> pairs;
  for (auto& f : files) pairs.emplace_back(f, f);  // keur:ignore KEUR-PORT-007 — std::pair<string,string> has a constructor — safe

  // No repo root → behave like rules_scan_files (file-level over the staged set).
  if (repo_root.empty()) {
    std::string root = ".";
    if (!files.empty()) {
      size_t slash = files[0].find_last_of('/');
      if (slash != std::string::npos) root = files[0].substr(0, slash);
    }
    Facts facts = derive_facts(root);
    return scan_impl(rules, pairs, facts);
  }

  // File-level fileset: the whole repo tree. file-absence/presence,
  // max-dir-files and filename-case are evaluated against THIS, so they answer
  // a repo question, not a diff question.
  std::vector<std::pair<std::string, std::string>> repo_files;
  walk(repo_root, "", repo_files);
  std::sort(repo_files.begin(), repo_files.end());

  Facts facts = derive_facts(repo_root);
  return scan_impl(rules, pairs, facts, &repo_files);
}

std::vector<RuleFinding> rules_scan_dir(const std::vector<Rule>& rules, const std::string& root) {
  std::vector<std::pair<std::string, std::string>> files;
  walk(root, "", files);
  std::sort(files.begin(), files.end());
  Facts facts = derive_facts(root);
  return scan_impl(rules, files, facts);
}

}  // namespace keur
