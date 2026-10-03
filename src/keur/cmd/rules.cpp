/**
 * @file cmd/rules.cpp
 * @brief keur rules list — the resolved rule cascade as a tree.
 */
#include "cmd/rules.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "common/platform.h"
#include "config/disabled.h"
#include "config/scopes.h"
#include "rules/rule_engine.h"
#include "util/keur_utils.h"

namespace keur::cmd {

namespace {

// A layer name for a resolved rule directory, inferred from its path, plus a
// specificity rank (higher = wins the cascade), mirroring CSS: built-in is the
// user-agent stylesheet (weakest), the explicit override is an inline style
// (strongest).
struct Layer {
  std::string dir;
  std::string name;
  int specificity;   // 1..4
};

Layer classify(const std::string& dir, int order_index) {
  // Heuristic from the path; falls back to the resolution order.
  if (dir.find("/.keur/rules") != std::string::npos)
    return {dir, "project  (.keur/rules)", 3};
  if (dir.find("/keur/rules") != std::string::npos &&
      (dir.find("/.config") != std::string::npos ||
       dir.find("XDG") != std::string::npos))
    return {dir, "global   (~/.config/keur/rules)", 2};
  // KEUR_HOME/rules is the built-in set; everything else that is neither
  // project nor global is treated as an explicit override.
  if (dir.find("/rules") != std::string::npos && order_index == 0)
    return {dir, "built-in (KEUR_HOME/rules)", 1};
  return {dir, "override (KEUR_RULES_DIRS)", 4};
}

const char* spec_bar(int s) {
  switch (s) {
    case 1: return "▁";
    case 2: return "▃";
    case 3: return "▆";
    default: return "█";
  }
}

// Resolve which keur.toml a --scope writes to. repo = <git-root>/keur.toml,
// global = $XDG_CONFIG_HOME/keur/keur.toml (fallback ~/.config). "" if
// unresolvable (e.g. repo scope outside a git repo).
std::string scope_target_path(const std::string& scope) {
  adapters::Git git{};
  std::string repo_root = git.run("rev-parse --show-toplevel");
  config::ScopeEnv env = config::capture_scope_env("", repo_root);
  if (scope == "global") return config::global_config_path(env);
  return config::repo_config_path(env);  // default: repo
}

// Add or remove a rule id from the [rules] disabled array in the scope's
// keur.toml, preserving the rest of the file. Returns 0 on success.
// (Definition is the public rules_toggle below; declared here for use above.)

}  // namespace

int rules_toggle(const std::string& id, bool disable, const std::string& scope) {
  if (id.empty()) { std::cerr << "keur rules: no rule id given\n"; return 1; }
  std::string path = scope_target_path(scope);
  if (path.empty()) {
    std::cerr << "keur rules: cannot resolve " << scope << " keur.toml"
                 " (not in a git repo?)\n";
    return 1;
  }
  adapters::Fs fs{};
  std::string body;
  { auto rr = fs.read_file(path); if (rr.ok) body = rr.content; }
  auto ids = config::parse_disabled(body);
  if (disable) ids.insert(id); else ids.erase(id);
  std::string updated = config::upsert_disabled_section(body, ids);
  // Ensure the parent directory exists (e.g. ~/.config/keur/ on a fresh
  // machine) — otherwise the write fails silently under the TUI. The dir is
  // everything up to the last platform separator in `path`.
  {
    std::string sep(1, keur::platform::path_sep());
    std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
      std::string dir = path.substr(0, slash);
      if (!dir.empty() && !fs.exists(dir)) fs.mkdir_p(dir);
    }
  }
  // Keep one backup so the previous version is always recoverable: copy the
  // existing keur.toml to keur.toml.bak (single slot, overwritten each save)
  // BEFORE writing the new content. Only when a file already exists.
  if (fs.exists(path)) {
    auto rr = fs.read_file(path);
    if (rr.ok) fs.write_file(path + ".bak", rr.content);
  }
  if (!fs.write_file(path, updated)) {
    std::cerr << "keur rules: could not write " << path << "\n";
    return 1;
  }
  std::cout << (disable ? "disabled " : "enabled ") << id
            << " in " << scope << " (" << path << ")\n";
  return 0;
}

namespace {
// Upsert `enabled = <val>` inside a [scanners.<name>] section of a keur.toml
// body, preserving everything else. If the section exists, set/replace its
// `enabled` line; otherwise append a new section. Pure string transform.
std::string upsert_scanner_enabled(const std::string& body, const std::string& name,
                                   bool enabled) {
  const std::string header = "[scanners." + name + "]";
  const std::string val = enabled ? "true" : "false";
  std::istringstream in(body);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);

  const std::size_t line_count = lines.size();
  int sec_start = -1, sec_end = static_cast<int>(line_count);
  for (int i = 0; i < static_cast<int>(line_count); ++i) {
    std::string t = lines[static_cast<std::size_t>(i)];
    // trim leading spaces for the header compare
    std::size_t a = t.find_first_not_of(" \t");
    std::string tt = (a == std::string::npos) ? "" : t.substr(a);
    if (sec_start < 0) {
      if (tt.rfind(header, 0) == 0) sec_start = i;
    } else if (!tt.empty() && tt.front() == '[') {  // next section ends ours
      sec_end = i; break;
    }
  }

  if (sec_start < 0) {
    // Append a fresh section.
    std::string out = body;
    if (!out.empty() && out.back() != '\n') out += "\n";
    out += "\n" + header + "\nenabled = " + val + "\n";
    return out;
  }
  // Look for an existing `enabled` line within the section; replace it.
  for (int i = sec_start + 1; i < sec_end; ++i) {
    std::string t = lines[static_cast<std::size_t>(i)];
    std::size_t a = t.find_first_not_of(" \t");
    std::string tt = (a == std::string::npos) ? "" : t.substr(a);
    if (tt.rfind("enabled", 0) == 0 && tt.find('=') != std::string::npos) {
      lines[static_cast<std::size_t>(i)] = "enabled = " + val;
      std::string out;
      for (std::size_t k = 0; k < lines.size(); ++k) { out += lines[k]; if (k + 1 < lines.size()) out += "\n"; }
      if (!body.empty() && body.back() == '\n') out += "\n";
      return out;
    }
  }
  // Section exists but no enabled line — insert one right after the header.
  lines.insert(lines.begin() + sec_start + 1, "enabled = " + val);
  std::string out;
  for (std::size_t k = 0; k < lines.size(); ++k) { out += lines[k]; if (k + 1 < lines.size()) out += "\n"; }
  if (!body.empty() && body.back() == '\n') out += "\n";
  return out;
}
}  // namespace

int scanners_toggle(const std::string& name, bool enable, const std::string& scope) {
  if (name.empty()) { std::cerr << "keur scanners: no scanner name given\n"; return 1; }
  std::string path = scope_target_path(scope);
  if (path.empty()) {
    std::cerr << "keur scanners: cannot resolve " << scope << " keur.toml\n";
    return 1;
  }
  adapters::Fs fs{};
  std::string body;
  { auto rr = fs.read_file(path); if (rr.ok) body = rr.content; }
  std::string updated = upsert_scanner_enabled(body, name, enable);
  // Ensure parent dir exists (fresh ~/.config/keur), then keep one backup.
  {
    std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
      std::string dir = path.substr(0, slash);
      if (!dir.empty() && !fs.exists(dir)) fs.mkdir_p(dir);
    }
  }
  if (fs.exists(path)) { auto rr = fs.read_file(path); if (rr.ok) fs.write_file(path + ".bak", rr.content); }
  if (!fs.write_file(path, updated)) {
    std::cerr << "keur scanners: could not write " << path << "\n";
    return 1;
  }
  std::cout << (enable ? "enabled " : "disabled ") << name
            << " in " << scope << " (" << path << ")\n";
  return 0;
}

int apply_scope(const std::string& scope,
                const std::vector<std::string>& disabled_rules,
                const std::vector<std::pair<std::string, bool>>& scanner_enabled) {
  std::string path = scope_target_path(scope);
  if (path.empty()) {
    std::cerr << "keur apply: cannot resolve " << scope << " keur.toml\n";
    return 1;
  }
  adapters::Fs fs{};
  std::string body;
  { auto rr = fs.read_file(path); if (rr.ok) body = rr.content; }

  // Plan → apply, all in memory, one write. Set the disabled-list to EXACTLY
  // the desired set (so re-enabling shrinks it), then set each scanner flag.
  std::set<std::string> ids(disabled_rules.begin(), disabled_rules.end());
  std::string updated = config::upsert_disabled_section(body, ids);
  for (const auto& [name, enabled] : scanner_enabled)
    updated = upsert_scanner_enabled(updated, name, enabled);

  // Ensure parent dir exists, keep one backup, write once.
  {
    std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
      std::string dir = path.substr(0, slash);
      if (!dir.empty() && !fs.exists(dir)) fs.mkdir_p(dir);
    }
  }
  if (fs.exists(path)) { auto rr = fs.read_file(path); if (rr.ok) fs.write_file(path + ".bak", rr.content); }
  if (!fs.write_file(path, updated)) {
    std::cerr << "keur apply: could not write " << path << "\n";
    return 1;
  }
  std::cout << "applied " << scope << " config (" << ids.size() << " rule(s) disabled, "
            << scanner_enabled.size() << " scanner(s) set) -> " << path << "\n";
  return 0;
}

RulesOptions rules_parse(const std::vector<std::string>& args) {
  RulesOptions o;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "list" || a == "disable" || a == "enable") o.subcommand = a;
    else if (a == "--winners") o.winners_only = true;
    else if (a == "--json") o.json = true;
    else if (a == "--scope" && i + 1 < args.size()) o.scope = args[++i];
    else if (!a.empty() && a[0] != '-') o.rule_id = a;  // positional id for disable/enable
  }
  return o;
}

int rules(const RulesOptions& opts) {
  // Write actions first: disable/enable toggle the [rules] disabled list.
  if (opts.subcommand == "disable") return rules_toggle(opts.rule_id, true, opts.scope);
  if (opts.subcommand == "enable")  return rules_toggle(opts.rule_id, false, opts.scope);

  auto dirs = rule_dirs();
  if (dirs.empty()) {
    std::cerr << "keur rules: no rule directories resolved "
                 "(set KEUR_HOME or add .keur/rules)\n";
    return 0;
  }

  // Resolve the disable-list once so the listing can mark each rule on/off.
  std::set<std::string> disabled;
  {
    adapters::Git git{};
    adapters::Fs fs{};
    std::string repo_root = git.run("rev-parse --show-toplevel");
    config::ScopeEnv env = config::capture_scope_env("", repo_root);
    auto clayers = config::resolve_layers(env, [&fs](const std::string& p) { return fs.exists(p); });
    disabled = config::resolve_disabled(clayers, [&fs](const std::string& p) {
      auto rr = fs.read_file(p); return rr.ok ? rr.content : std::string();
    });
  }

  // Load each layer and track, per rule id, which layer currently wins (the
  // highest specificity seen). This is the cascade: a later, more specific
  // layer overrides an earlier one with the same id.
  struct Winner { int specificity; std::string layer; };
  std::map<std::string, Winner> winner;

  struct LoadedLayer { Layer layer; std::vector<Rule> rules; };
  std::vector<LoadedLayer> layers;

  int idx = 0;
  for (const auto& d : dirs) {
    Layer L = classify(d, idx++);
    auto rs = rules_load(d);
    for (const auto& r : rs) {
      auto it = winner.find(r.id);
      if (it == winner.end() || L.specificity >= it->second.specificity)
        winner[r.id] = {L.specificity, L.name};
    }
    layers.push_back({L, std::move(rs)});
  }

  std::cout << "keur rule cascade  (specificity: built-in ▁ < global ▃ < "
               "project ▆ < override █; higher wins)\n\n";

  int total = 0, shadowed = 0;
  for (const auto& LL : layers) {
    std::cout << spec_bar(LL.layer.specificity) << " " << LL.layer.name
              << "   [" << LL.layer.dir << "]\n";
    if (LL.rules.empty()) {
      std::cout << "  (no rules)\n\n";
      continue;
    }
    // group by category for a readable tree
    std::map<std::string, std::vector<const Rule*>> by_cat;
    for (const auto& r : LL.rules) by_cat[r.category].push_back(&r);

    for (const auto& [cat, rs] : by_cat) {
      std::cout << "  " << (cat.empty() ? "(uncategorised)" : cat) << "\n";
      for (const auto* r : rs) {
        ++total;
        bool wins = winner[r->id].specificity == LL.layer.specificity &&
                    winner[r->id].layer == LL.layer.name;
        const char* mark = wins ? "✓" : "·";   // · = shadowed by a stronger layer
        if (!wins) ++shadowed;
        if (opts.winners_only && !wins) continue;
        const bool is_off = disabled.contains(r->id);
        std::cout << "    " << (is_off ? "✗" : mark) << " " << r->id;
        if (!r->severity.empty()) std::cout << "  [" << r->severity << "]";
        if (!r->title.empty())    std::cout << "  " << r->title;
        if (is_off) std::cout << "   (disabled)";
        else if (!wins)
          std::cout << "   (shadowed by " << winner[r->id].layer << ")";
        std::cout << "\n";
      }
    }
    std::cout << "\n";
  }

  std::cout << total << " rule(s) across " << layers.size() << " layer(s)";
  if (shadowed) std::cout << "; " << shadowed << " shadowed by a more specific layer";
  std::cout << ".\n";
  return 0;
}

}  // namespace keur::cmd
