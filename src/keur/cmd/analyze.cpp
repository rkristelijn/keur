/**
 * @file cmd/analyze.cpp
 * @brief keur analyze literals — see analyze.h.
 */
#include "cmd/analyze.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "util/keur_utils.h"

namespace keur::cmd {

namespace fs = std::filesystem;

AnalyzeOptions analyze_parse(const std::vector<std::string>& args) {
  AnalyzeOptions o;
  bool got_target = false;
  for (size_t i = 0; i < args.size(); i++) {
    const std::string& a = args[i];
    if (a == "--magic") o.magic_only = true;
    else if (a == "--identity") o.identity_only = true;
    else if (a == "--min-count" && i + 1 < args.size()) o.min_count = std::atoi(args[++i].c_str());
    else if (!a.empty() && a[0] == '-') continue;  // unknown flag, ignore
    else if (o.kind.empty()) o.kind = a;           // first positional = kind
    else if (!got_target) { o.target = a; got_target = true; }
  }
  return o;
}

namespace {

// Load the generic classify rule (ADR-059) by id from the rule dirs. We load
// raw (not the active/filtered set) because generic rules are inert there.
bool load_classify_rule(const std::string& id, Rule& out) {
  for (const auto& d : keur::rule_dirs())
    for (auto& r : keur::rules_load(d))
      if (r.id == id && r.generic) { out = r; return true; }
  return false;
}

// Collect scannable files under a path (file or dir), skipping VCS/build dirs.
std::vector<std::string> collect_files(const std::string& target) {
  std::vector<std::string> files;
  std::error_code ec;
  if (fs::is_regular_file(target, ec)) { files.push_back(target); return files; }
  for (auto it = fs::recursive_directory_iterator(target, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const auto& p = it->path();
    std::string s = p.string();
    if (s.find("/.git/") != std::string::npos || s.find("/node_modules/") != std::string::npos ||
        s.find("/build/") != std::string::npos || s.find("/dist/") != std::string::npos ||
        s.find("/vendor/") != std::string::npos || s.find("/.tmp/") != std::string::npos) {
      continue;
    }
    if (it->is_regular_file(ec)) files.push_back(s);
  }
  return files;
}

void print_bucket(const std::string& title, const std::string& bucket,
                  std::vector<ClassifiedLiteral>& rows, int min_count) {
  // filter + sort: identity by rarity asc (rare = suspicious); magic by count desc.
  std::vector<ClassifiedLiteral*> sel;
  for (auto& r : rows)
    if (r.bucket == bucket && r.count >= min_count) sel.push_back(&r);
  if (sel.empty()) return;
  if (bucket == "magic")
    std::sort(sel.begin(), sel.end(), [](auto* a, auto* b){ return a->count > b->count; });
  else
    std::sort(sel.begin(), sel.end(), [](auto* a, auto* b){ return a->count < b->count; });

  std::cout << "\n== " << title << " (" << sel.size() << ") ==\n";
  for (auto* r : sel) {
    std::cout << "  " << r->count << "x  [" << r->klass << "]  " << r->text << "\n";
    std::cout << "        " << r->file << ":" << r->line;
    if (!r->advice.empty()) std::cout << "  — " << r->advice;
    std::cout << "\n";
  }
}

}  // namespace

int analyze(const AnalyzeOptions& opts) {
  if (opts.kind != "literals") {
    std::cerr << "keur analyze: unknown analysis '" << opts.kind
              << "' (available: literals)\n";
    return 0;
  }

  Rule rule;
  if (!load_classify_rule("literal-classes", rule)) {
    std::cerr << "keur analyze literals: no generic rule 'literal-classes' found "
                 "(set KEUR_HOME or KEUR_RULES_DIRS)\n";
    return 0;
  }

  auto files = collect_files(opts.target);
  auto rows = run_literal_pipeline(rule, files);

  bool show_identity = !opts.magic_only;
  bool show_magic = !opts.identity_only;

  if (show_identity)
    print_bucket("IDENTITY — externalise (config/vault, not source)", "identity", rows, opts.min_count);
  if (show_magic)
    print_bucket("MAGIC — centralise to a constant/enum (rule of three)", "magic", rows, std::max(opts.min_count, 1));

  // summary counts (noise is intentionally not printed)
  int n_id = 0, n_mg = 0;
  for (auto& r : rows) {
    if (r.bucket == "identity") n_id++;
    else if (r.bucket == "magic") n_mg++;
  }
  std::cerr << "keur analyze literals: " << files.size() << " file(s), "
            << rows.size() << " unique literal(s) — "
            << n_id << " identity, " << n_mg << " magic\n";
  return 0;
}

}  // namespace keur::cmd
