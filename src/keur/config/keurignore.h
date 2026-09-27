/**
 * @file config/keurignore.h
 * @brief .keurignore — the SINGLE standalone suppression file (like
 *        .gitleaksignore / .semgrepignore), in the repo root or .config/.
 * @implements ADR-035
 *
 * Two suppression layers exist (ADR-035):
 *   1. inline  `keur:ignore <RULE-ID>` on the offending code line   (in-diff)
 *   2. `.keurignore` file — the one place for repo-level suppression.
 *      (keur.toml `[rules] disabled` still works but .keurignore is preferred;
 *       all suppression lives here.)
 *
 * CRUCIAL distinction by category (the maintainer's rule):
 *   - SECRETS suppress by VALUE, never by line. A moved secret stays suppressed;
 *     a CHANGED secret is caught again. A line-based entry MUST NOT silence a
 *     secret — so `secret_values` is the only thing that suppresses category
 *     "secret"/"secrets".
 *   - CODE issues suppress by LOCATION (file[:line]) and/or rule id — there the
 *     code IS the value, so a location entry is correct.
 *
 * A `.keurignore` line is one of:
 *   secret <VALUE|sha256:HASH>        suppress a specific secret value (any file)
 *   RULE-ID                            suppress that (non-secret) rule everywhere
 *   path/glob                          suppress non-secret findings under a path
 *   path/glob:RULE-ID                  that rule under that path
 *   path/file.ext:LINE                 a specific code finding at file:line
 *   path/file.ext:LINE:RULE-ID         that rule at that exact line
 *   # comment / blank                  ignored
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur::config {

/** One parsed .keurignore entry. */
struct IgnoreEntry {
    // Secret-value suppression (robust across moves; broken by a value change).
    std::string secret_value;  ///< literal secret OR "sha256:<hex>" ("" = not a secret entry)
    // Code-issue suppression (location + rule).
    std::string path;          ///< path prefix/substring ("" = any)
    int         line = 0;      ///< exact line (0 = any line in the path)
    std::string rule;          ///< rule id ("" = any rule)
};

/** Parse a .keurignore body into entries. Pure — no I/O. */
[[nodiscard]] std::vector<IgnoreEntry> parse_keurignore(const std::string& body);

/**
 * Does any entry suppress this finding? Pure.
 * @param category  finding category — "secret"/"secrets" gate value-only suppression.
 * @param matched   the matched text (the secret value, for secret findings).
 * A SECRET finding is suppressed ONLY by a secret_value entry whose value (or
 * sha256) equals `matched` — never by a path/line entry. A non-secret finding
 * is suppressed by a matching path/line/rule entry (never by a secret entry).
 */
[[nodiscard]] bool keurignore_suppresses(const std::vector<IgnoreEntry>& entries,
                                         const std::string& category,
                                         const std::string& file, int line,
                                         const std::string& rule,
                                         const std::string& matched);

/** Resolve .keurignore from repo root, then .config/. First existing wins. */
[[nodiscard]] std::vector<IgnoreEntry> resolve_keurignore(
    const std::string& repo_root,
    const std::function<std::string(const std::string& path)>& read_file);

}  // namespace keur::config
