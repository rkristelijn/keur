/**
 * @file config/scopes.h
 * @brief Config layer stack — resolve every keur.toml layer and its source.
 *
 * keur config resolves in layers (README "Configuration"): built-in defaults →
 * global ($XDG_CONFIG_HOME/keur/keur.toml) → $KEUR_HOME/config/keur.toml →
 * repo (<git-root>/keur.toml) → an explicit --config path. `cmd/policy.cpp`
 * already collapses this into a single "winning" path via resolve_config();
 * this module exposes the *whole* stack so `keur config list` and the manager
 * TUI can show every layer and where a change would be written.
 *
 * Resolution is a pure function of a ScopeEnv snapshot (the env vars + cwd/repo
 * root), so it is unit-testable without touching the real environment. I/O
 * (does the file exist?) is injected via a predicate, keeping this layer pure
 * and spoofable — the same testability model as the adapters (ADR-003).
 *
 * @id KEUR-CONFIG-SCOPES
 * @implements ADR-003
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur::config {

/// Which layer a config file belongs to, general → specific (later overrides
/// earlier). `Explicit` (a --config path) always wins when present.
enum class Scope { Defaults, Global, KeurHome, Repo, Explicit };

/// One resolved layer in the stack.
struct Layer {
    Scope       scope;
    std::string path;          ///< resolved keur.toml path ("" for Defaults)
    bool        exists = false;///< whether the file is present on disk

    Layer() = default;
    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    Layer(Scope scope_, std::string path_, bool exists_)
        : scope(scope_), path(std::move(path_)), exists(exists_) {}
};

/// Snapshot of the environment the resolution depends on. Captured once so the
/// resolver is a pure function (no getenv scattered through the logic).
struct ScopeEnv {
    std::string xdg_config_home; ///< $XDG_CONFIG_HOME ("" if unset)
    std::string home;            ///< $HOME ("" if unset)
    std::string keur_home;       ///< $KEUR_HOME ("" if unset)
    std::string repo_root;       ///< git repo root ("" if not in a repo)
    std::string explicit_path;   ///< --config value ("" if not given)
};

/// Capture the ScopeEnv from the real process environment + git. The repo_root
/// is resolved via the injected git-root query (defaults to the real git
/// adapter at the call site in cmd/config.cpp).
ScopeEnv capture_scope_env(const std::string& explicit_path,
                           const std::string& repo_root);

/// Path of the global keur.toml for a given ScopeEnv (XDG, else ~/.config).
[[nodiscard]] std::string global_config_path(const ScopeEnv& env);

/// Path of the $KEUR_HOME config, or "" when KEUR_HOME is unset.
[[nodiscard]] std::string keur_home_config_path(const ScopeEnv& env);

/// Path of the repo keur.toml, or "" when not inside a repo.
[[nodiscard]] std::string repo_config_path(const ScopeEnv& env);

/// Human-readable name of a scope ("defaults", "global", …). For display/JSON.
[[nodiscard]] std::string scope_name(Scope scope);

/**
 * Resolve the full layer stack (general → specific) for a ScopeEnv.
 * `file_exists` reports whether a given path is present (inject a fake in
 * tests; production passes the Fs adapter's exists). The Defaults layer is
 * always first and has an empty path. An explicit --config path, when set, is
 * appended as the final `Explicit` layer.
 */
[[nodiscard]] std::vector<Layer> resolve_layers(
    const ScopeEnv& env,
    const std::function<bool(const std::string&)>& file_exists);

}  // namespace keur::config
