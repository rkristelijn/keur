/**
 * @file util/keur_utils.h
 * @brief Shared helpers used across keur subcommands.
 *
 * Keeping these in one place prevents duplication between cmd_check.cpp,
 * cmd_scan.cpp, and future subcommands.
 */
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "common/platform.h"

namespace keur {

/**
 * Resolve keur's home directory — the root of a keur installation, holding
 * `rules/` and `config/`. Resolution (first hit wins):
 *   1. $KEUR_HOME               explicit override (unchanged behaviour)
 *   2. derived from the binary  <dir-of-exe>/..  when it looks like an install
 *      (i.e. the exe sits in <home>/bin/ and <home>/rules or
 *      <home>/config/keur.toml exists) — so keur finds its own rules with NO
 *      env var set, the way git locates its support files relative to the
 *      binary. @see ADR-063.
 * Returns "" when neither applies (keur then falls back to the global/project
 * layers only). This makes $KEUR_HOME optional instead of required — fixing the
 * silent-gate regression where an unset KEUR_HOME loaded zero built-in rules.
 */
inline std::string derive_keur_home() {
    namespace fs = std::filesystem;
    std::error_code ec;
    // 1. explicit override.
    if (const char* h = std::getenv("KEUR_HOME")) {
        if (*h) return h;
    }
    // 2. derive from the running binary's location.
    std::string exe = platform::executable_path();
    if (exe.empty()) return "";
    fs::path bin_dir = fs::path(exe).parent_path();            // <...>/bin
    fs::path home = bin_dir.parent_path();                     // <...>
    if (home.empty()) return "";
    // Confirm it really is a keur install, not just any bin/ dir: require a
    // rules/ dir or a config/keur.toml marker under the candidate home.
    if (fs::is_directory(home / "rules", ec) ||
        fs::is_regular_file(home / "config" / "keur.toml", ec)) {
        return home.string();
    }
    return "";
}

/**
 * JSON-escape a string (backslash, double-quote, newline, tab).
 * Matches the escaping used by keur-rules/main.cpp.
 */
inline std::string jesc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"':  o += "\\\""; break;
            case '\n': o += ' ';    break;
            case '\t': o += ' ';    break;
            default:   o += c;
        }
    }
    return o;
}

/**
 * Return the list of rule directories to search.
 *
 * Resolution order (general -> specific; later dirs augment/override earlier
 * ones, mirroring how keur.toml config layers resolve):
 *   1. $KEUR_HOME/rules                     built-in rules (shipped with keur)
 *   2. $XDG_CONFIG_HOME/keur/rules          global / company rules (per machine)
 *      (falls back to ~/.config/keur/rules)
 *   2b. ~/.keur/rules                       personal cross-repo PRIVATE rules
 *      (e.g. a BII/PII denylist; outside every repo, auto-loaded if present)
 *   3. <git-root-or-cwd>/.keur/rules        project rules (travel with the repo)
 *   4. each colon-separated entry in $KEUR_RULES_DIRS   explicit override
 *
 * The project layer is discovered by walking up from the current directory
 * until a `.keur/rules` directory is found (like git locating `.git`), so it
 * works from any subdirectory of the repo.
 */
inline std::string find_project_rules_dir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    if (ec) return "";
    for (; !cur.empty(); cur = cur.parent_path()) {
        fs::path cand = cur / ".keur" / "rules";
        if (fs::is_directory(cand, ec)) return cand.string();
        if (cur == cur.root_path()) break;
    }
    return "";
}

inline std::vector<std::string> rule_dirs() {
    std::vector<std::string> dirs;
    // 1. built-in — $KEUR_HOME if set, else derived from the binary location
    //    (ADR-063), so built-in rules load with no env var required.
    {
        std::string home = derive_keur_home();
        if (!home.empty()) dirs.push_back(home + "/rules");
    }
    // 2. global / company: $XDG_CONFIG_HOME/keur/rules (fallback ~/.config)
    {
        std::string global_base;
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
            global_base = xdg;
        else if (const char* h = std::getenv("HOME"))
            global_base = std::string(h) + "/.config";
        if (!global_base.empty())
            dirs.push_back(global_base + "/keur/rules");
    }
    // 2b. personal: ~/.keur/rules — a fixed, convention-over-config home for
    //     cross-repo PRIVATE rules (e.g. a BII/PII denylist of literal forbidden
    //     terms you must never commit anywhere). It lives OUTSIDE every repo, so
    //     git can never stage it and keur's scan-walker never sees it. Missing
    //     dir → silently skipped (rules_load on a non-existent dir yields none),
    //     so a machine without the file simply has no personal layer.
    if (const char* h = std::getenv("HOME"))
        dirs.push_back(std::string(h) + "/.keur/rules");
    // 3. project: nearest .keur/rules walking up from CWD
    {
        std::string proj = find_project_rules_dir();
        if (!proj.empty()) dirs.push_back(proj);
    }
    // 4. explicit override(s)
    if (const char* extra = std::getenv("KEUR_RULES_DIRS")) {
        std::string s(extra), cur;
        for (char c : s) {
            if (c == ':') {
                if (!cur.empty()) { dirs.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) dirs.push_back(cur);
    }
    return dirs;
}

}  // namespace keur
