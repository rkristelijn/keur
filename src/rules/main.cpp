/**
 * @file main.cpp
 * @brief keur-rules CLI — the native rule engine entry point.
 *
 * Usage:
 *   keur-rules --files <f1> <f2> ...     scan an explicit file list (hook/diff)
 *   keur-rules --dir <root>              scan a project directory
 *   Rule dirs: KEUR_HOME/rules plus each dir in KEUR_RULES_DIRS (':'-separated).
 *
 * Output: one KEUR Finding JSON object per line on stdout. Same shape the
 * evidence/policy layers already consume from scanners/rules.sh.
 */
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "rule_engine.h"
#include "rule/facts.h"

using namespace keur;

static std::string jesc(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"': o += "\\\""; break;
      case '\n': o += ' '; break;
      case '\t': o += ' '; break;
      default: o += c;
    }
  }
  return o;
}

static std::vector<std::string> rule_dirs() {
  std::vector<std::string> dirs;
  if (const char* home = std::getenv("KEUR_HOME")) dirs.push_back(std::string(home) + "/rules");
  if (const char* extra = std::getenv("KEUR_RULES_DIRS")) {
    std::string s(extra), cur;
    for (char c : s) {
      if (c == ':') { if (!cur.empty()) dirs.push_back(cur); cur.clear(); }
      else cur += c;
    }
    if (!cur.empty()) dirs.push_back(cur);
  }
  return dirs;
}

int main(int argc, char** argv) {
  std::vector<std::string> files;
  std::string dir;
  bool facts_only = false;
  enum { NONE, FILES, DIR } mode = NONE;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--files") mode = FILES;
    else if (a == "--dir") { mode = DIR; if (i + 1 < argc) dir = argv[++i]; }
    else if (a == "--facts") facts_only = true;
    else if (mode == FILES) files.push_back(a);
  }
  if (mode == NONE) {
    std::cerr << "usage: keur-rules --files <f...> | --dir <root> [--facts]\n";
    return 2;
  }

  // --facts (ADR-009 debug): derive and print the facts map, then stop. Lets the
  // facts pass be verified in isolation before it gates any matching.
  if (facts_only) {
    std::string root = dir.empty() ? (files.empty() ? "." : files[0]) : dir;
    std::cout << facts_to_json(derive_facts(root)) << "\n";
    return 0;
  }

  // Load rules from all configured dirs.
  std::vector<Rule> rules;
  for (auto& d : rule_dirs()) {
    auto rs = rules_load(d);
    for (auto& r : rs) rules.push_back(std::move(r));
  }
  if (rules.empty()) return 0;

  std::vector<RuleFinding> findings =
      (mode == DIR) ? rules_scan_dir(rules, dir) : rules_scan_files(rules, files);

  for (auto& f : findings) {
    std::cout << "{\"source\":\"keur-rules\""
              << ",\"rule\":\"" << jesc(f.rule_id) << "\""
              << ",\"title\":\"" << jesc(f.title) << "\""
              << ",\"category\":\"" << jesc(f.category) << "\""
              << ",\"severity\":\"" << jesc(f.severity) << "\""
              << ",\"action\":\"" << jesc(f.action) << "\""
              << ",\"confidence\":" << (f.confidence.empty() ? "0.5" : f.confidence)
              << ",\"deterministic\":true"
              << ",\"file\":\"" << jesc(f.file) << "\""
              << ",\"line\":" << f.line
              << ",\"message\":\"" << jesc(f.message) << "\"}"
              << "\n";
  }
  return 0;
}
