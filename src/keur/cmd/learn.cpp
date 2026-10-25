/**
 * @file cmd/learn.cpp
 * @brief keur learn — serve a rule's explainer to the user/AI (ADR-036).
 *
 * Orchestration lives here; decision logic (id validation, path building,
 * fallback formatting) is factored into pure helpers so it is unit-testable.
 * All file I/O goes through the Fs adapter and the one network side effect
 * (opt-in curl) through the Proc adapter (ADR-003 / KEUR-ARCH-001).
 */
#include "cmd/learn.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/proc.h"
#include "cmd/train.h"
#include "common/platform.h"
#include "config/ruleset.h"
#include "util/keur_utils.h"
#include "rules/rule_engine.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

bool is_valid_rule_id(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  bool prev_hyphen = false;
  for (char c : s) {
    const bool ok_char = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok_char) return false;              // no lowercase, dots, slashes, spaces
    if (c == '-' && prev_hyphen) return false;  // no "--"
    prev_hyphen = (c == '-');
  }
  return s.front() != '-' && s.back() != '-';  // no leading/trailing hyphen
}

std::string explainer_path(const std::string& base_dir, const std::string& id) {
  return keur::platform::path_join(base_dir, id + ".md");
}

namespace {

std::string keur_home() {
  std::string h = keur::derive_keur_home();
  return h.empty() ? "." : h;
}

// $XDG_CACHE_HOME/keur/rules, falling back to $HOME/.cache/keur/rules. Empty
// only if neither is resolvable (then caching is skipped, not fatal).
std::string cache_rules_dir() {
  if (const char* x = std::getenv("XDG_CACHE_HOME")) return std::string(x) + "/keur/rules";
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.cache/keur/rules";
  return "";
}

// The remote base for a one-off explainer download. Overridable so a fork/org
// can point elsewhere; defaults to the canonical keur repo raw path (ADR-036).
std::string remote_base() {
  if (const char* u = std::getenv("KEUR_LEARN_URL_BASE")) return u;
  return "https://raw.githubusercontent.com/rkristelijn/keur/main/docs/rules";
}

bool fetch_opted_in(bool flag_fetch) {
  if (flag_fetch) return true;
  const char* e = std::getenv("KEUR_LEARN_FETCH");
  return e && std::string(e) == "1";
}

// Print an explainer body verbatim (it is markdown; the terminal renders raw).
void print_body(const std::string& body) { std::cout << body; }

// Deterministic fallback: find the rule by id and print its own metadata.
// Always useful when no explainer markdown exists. Returns exit code.
int print_rule_fallback(const std::string& id) {
  std::vector<Rule> rules = keur::config::load_active_rules();
  const Rule* hit = nullptr;
  for (const auto& r : rules) {
    if (r.id == id) { hit = &r; break; }
  }
  if (!hit) {
    std::cerr << "keur learn: no explainer and no rule found for '" << id
              << "'. Check the id (e.g. KEUR-CMDI-003), or fetch with --fetch.\n";
    return 1;
  }
  std::cout << "# " << hit->id;
  if (!hit->title.empty()) std::cout << " — " << hit->title;
  std::cout << "\n";
  if (!hit->category.empty() || !hit->severity.empty()) {
    std::cout << "category: " << (hit->category.empty() ? "-" : hit->category)
              << " · severity: " << (hit->severity.empty() ? "-" : hit->severity)
              << "\n";
  }
  std::cout << "\n(no explainer markdown yet — showing the rule's own fields.\n"
               " Add docs/rules/" << id << ".md for the full write-up.)\n\n";
  if (!hit->message.empty()) std::cout << "## What fires\n" << hit->message << "\n\n";
  if (!hit->fix.empty())     std::cout << "## Fix\n" << hit->fix << "\n\n";
  std::cout << "## Suppress (if a genuine false positive)\n"
               "`# keur:ignore " << id << "`  (or a .keurignore entry — ADR-035)\n";
  return 0;
}

// Try to fetch <id>.md from the remote into cache_path (shell-free curl).
// Returns true on success. Only ever called when the user opted in.
bool try_remote_fetch(adapters::Fs& fs, adapters::Proc& proc,
                      const std::string& id, const std::string& cache_dir,
                      const std::string& cache_path) {
  if (cache_dir.empty()) return false;
  if (!proc.has("curl")) {
    std::cerr << "keur learn: --fetch needs curl on PATH.\n";
    return false;
  }
  fs.mkdir_p(cache_dir);
  const std::string url = remote_base() + "/" + id + ".md";  // keur:ignore KEUR-PORT-004 — URL path, always '/', not a filesystem path
  std::cerr << "keur learn: fetching " << url << " (opt-in)…\n";
  // Shell-free: curl + literal args in argv, no /bin/sh (CWE-78, KEUR-SEC-006).
  // -f: fail on HTTP error; -sS: quiet but show errors; -L: follow redirects;
  // -o: write to the cache path. curl only downloads — it never uploads.
  const int rc = proc.run_argv(
      {"curl", "-fsSL", "-o", cache_path, url}, {});
  if (rc != 0) {
    std::cerr << "keur learn: fetch failed (rc=" << rc << "). No explainer online for '"
              << id << "'?\n";
    return false;
  }
  return fs.exists(cache_path);
}

// Resolve + print the explainer for `id`: local -> cache -> opt-in remote,
// then deterministic .rule fallback. Returns process exit code.
int show_explainer(const std::string& id, bool flag_fetch) {
  if (!is_valid_rule_id(id)) {
    std::cerr << "keur learn: '" << id << "' is not a valid rule id "
                 "(expected e.g. KEUR-CMDI-003).\n";
    return 1;
  }
  adapters::Fs fs;
  adapters::Proc proc;

  // 1. local: shipped with the repo/binary.
  const std::string local = explainer_path(keur_home() + "/docs/rules", id);
  if (auto r = fs.read_file(local); r.ok) { print_body(r.content); return 0; }

  // 2. cache: a previously fetched explainer.
  const std::string cache_dir = cache_rules_dir();
  const std::string cached = cache_dir.empty() ? "" : explainer_path(cache_dir, id);
  if (!cached.empty()) {
    if (auto r = fs.read_file(cached); r.ok) { print_body(r.content); return 0; }
  }

  // 3. remote: only when the user opted in; cache then print.
  if (fetch_opted_in(flag_fetch) && !cached.empty()) {
    if (try_remote_fetch(fs, proc, id, cache_dir, cached)) {
      if (auto r = fs.read_file(cached); r.ok) { print_body(r.content); return 0; }
    }
  }

  // 4. fallback: the rule's own fields (always useful, offline).
  return print_rule_fallback(id);
}

}  // namespace

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

int learn(const std::vector<std::string>& args) {
  bool flag_fetch = false;
  std::string rule_id;
  for (const auto& a : args) {
    if (a == "--fetch") flag_fetch = true;
    else if (!a.empty() && a[0] != '-' && rule_id.empty()) rule_id = a;
  }

  // With a rule id: the new behaviour — serve the explainer.
  if (!rule_id.empty()) return show_explainer(rule_id, flag_fetch);

  // No rule id: keep the deprecated alias to `keur train` for one release, so
  // running loops (supervise.sh / nightly `keur learn`) keep working (ADR-036).
  std::cerr << "keur learn: no rule id given. NOTE: the optimise loop is now "
               "'keur train' (ADR-036); forwarding there. Use 'keur learn "
               "<RULE-ID>' to read a rule's explainer.\n";
  return train(train_parse(args));
}

}  // namespace keur::cmd
