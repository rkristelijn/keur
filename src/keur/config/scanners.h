/**
 * @file config/scanners.h
 * @brief Inventory of external scanners (ADR-019): what keur.toml declares, is
 *        each tool on PATH, and which of its config/ignore files exist.
 *
 * keur orchestrates proven tools (gitleaks, semgrep, trivy, checkov, …) as
 * evidence sources. This module reads the [scanners.*] inventory from a
 * keur.toml body, checks PATH availability, and detects each tool's own
 * dotfiles (.gitleaksignore, .semgrepignore, trivy.yaml, …) so the manager can
 * show and later manage them. Read-only here.
 *
 * Pure + injectable: the toml body is passed in, PATH availability via a
 * `tool_on_path` callable, file existence via an `exists` callable — so the
 * whole inventory is unit-testable with no real tools and no real files.
 * @implements ADR-003 ADR-019
 * @id KEUR-SCANNERS
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur::config {

/// One config/ignore file a scanner may use, and whether it is present.
struct ScannerConfigFile {
    std::string path;         ///< repo-relative (e.g. ".gitleaksignore")
    bool        present = false;
};

/// A scanner as declared in keur.toml + resolved runtime state.
struct Scanner {
    std::string name;             ///< section name ("gitleaks")
    std::string bin;              ///< executable to resolve on PATH
    std::string group;           ///< "secrets" | "sast" | "deps" | "iac" | …
    std::string default_action;   ///< "block" | "warn" | ""
    bool        enabled = true;   ///< [scanners.x] enabled = false → false
    bool        on_path = false;   ///< resolved: is `bin` available?
    std::vector<ScannerConfigFile> config_files;  ///< known dotfiles + presence
};

/// Callables the inventory needs — spoofable in tests.
struct ScannerProbe {
    std::function<bool(const std::string& tool)> tool_on_path;  ///< is tool on PATH?
    std::function<bool(const std::string& path)> exists;        ///< file exists?
    std::string repo_root;                                      ///< for dotfile paths ("" = cwd-relative)
};

/// Parse the [scanners.*] sections from a keur.toml body into Scanner records
/// (name/bin/group/action/enabled only — no I/O). Pure.
[[nodiscard]] std::vector<Scanner> parse_scanners(const std::string& toml_body);

/// The scanner names listed for a tier ([tiers.<tier>] scanners = [...]) in a
/// keur.toml body. This is what keeps a heavy tool (semgrep) out of the fast
/// pre-commit gate: only the tools named for the active tier are dispatched.
/// Returns {} when the tier or its scanners key is absent (caller falls back to
/// a sensible default). Pure. @id KEUR-TIER-SCANNERS
[[nodiscard]] std::vector<std::string> tier_scanners(const std::string& toml_body,
                                                     const std::string& tier);

/// The known config/ignore files for a scanner name (data-driven; empty when
/// none are known). E.g. "gitleaks" → {".gitleaks.toml", ".gitleaksignore"}.
[[nodiscard]] std::vector<std::string> known_config_files(const std::string& scanner_name);

/// A one-line hint for installing a tool that is not on PATH, tailored to the
/// current OS (Homebrew on macOS, apt on Debian/Ubuntu, apk on Alpine). Returns
/// "" when no hint is known. Data-driven — a new tool is one row. @id KEUR-SCANNER-INSTALL
[[nodiscard]] std::string install_hint(const std::string& scanner_name);

/// The built-in default scanner inventory (name + group + default-action),
/// used when no keur.toml template is resolvable so the manager's Scanners
/// branch is never empty — the same "baked default" idea as the rule catalogue.
[[nodiscard]] std::vector<Scanner> default_scanners();

/// Resolve runtime state for each scanner: PATH availability + which known
/// config files exist. Returns the enriched inventory. Pure over the probe.
[[nodiscard]] std::vector<Scanner> resolve_scanners(const std::vector<Scanner>& declared,
                                                    const ScannerProbe& probe);

}  // namespace keur::config
