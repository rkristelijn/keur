/**
 * @file config/scopes.cpp
 * @brief Pure resolution of the keur.toml layer stack. No I/O of its own —
 *        file existence is injected, env is captured up front — so every path
 *        here is unit-testable with a spoofed ScopeEnv + predicate.
 *
 * @implements KEUR-CONFIG-SCOPES
 */
#include "config/scopes.h"

#include <cstdlib>

#include "common/platform.h"

namespace keur::config {

namespace {

// $XDG_CONFIG_HOME if set, else $HOME/.config, else "" (no base resolvable).
std::string config_base(const ScopeEnv& env) {
    if (!env.xdg_config_home.empty()) return env.xdg_config_home;
    if (!env.home.empty()) return platform::path_join(env.home, ".config");
    return "";
}

}  // namespace

ScopeEnv capture_scope_env(const std::string& explicit_path,
                           const std::string& repo_root) {
    ScopeEnv env;
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) env.xdg_config_home = x;
    if (const char* h = std::getenv("HOME")) env.home = h;
    if (const char* k = std::getenv("KEUR_HOME")) env.keur_home = k;
    env.repo_root = repo_root;
    env.explicit_path = explicit_path;
    return env;
}

std::string global_config_path(const ScopeEnv& env) {
    std::string base = config_base(env);
    if (base.empty()) return "";
    return platform::path_join(platform::path_join(base, "keur"), "keur.toml");
}

std::string keur_home_config_path(const ScopeEnv& env) {
    if (env.keur_home.empty()) return "";
    return platform::path_join(platform::path_join(env.keur_home, "config"),
                               "keur.toml");
}

std::string repo_config_path(const ScopeEnv& env) {
    if (env.repo_root.empty()) return "";
    return platform::path_join(env.repo_root, "keur.toml");
}

// Repo-scoped config in a .config/ subdirectory, to keep the project root
// uncluttered (the XDG-in-repo convention: tools look under <repo>/.config/ so
// the root does not fill with per-tool dotfiles — see nodejs/tooling#71). This
// resolves to <repo>/.config/keur.toml and is tried BEFORE the root
// <repo>/keur.toml, so a project can keep its keur config out of the root.
std::string repo_dotconfig_path(const ScopeEnv& env) {
    if (env.repo_root.empty()) return "";
    return platform::path_join(platform::path_join(env.repo_root, ".config"),
                               "keur.toml");
}

std::string scope_name(Scope scope) {
    switch (scope) {
        case Scope::Defaults: return "defaults";
        case Scope::Global:   return "global";
        case Scope::KeurHome: return "keur-home";
        case Scope::Repo:     return "repo";
        case Scope::Explicit: return "explicit";
    }
    return "unknown";
}

std::vector<Layer> resolve_layers(
    const ScopeEnv& env,
    const std::function<bool(const std::string&)>& file_exists) {
    std::vector<Layer> layers;

    // Defaults: always present, no file backs it (built into the binary).
    layers.push_back({Scope::Defaults, "", true});

    // Each optional file layer is added when its path is resolvable; `exists`
    // records whether the file is actually on disk (a layer can be "known but
    // absent", which the UI shows as a place a config *could* live).
    auto add = [&layers, &file_exists](Scope scope, const std::string& path) {
        if (path.empty()) return;
        layers.push_back({scope, path, file_exists(path)});
    };

    add(Scope::Global, global_config_path(env));
    add(Scope::KeurHome, keur_home_config_path(env));
    // Repo scope, uncluttered-first: <repo>/.config/keur.toml is tried before
    // the root <repo>/keur.toml, so both work and .config/ keeps the root clean.
    add(Scope::Repo, repo_dotconfig_path(env));
    add(Scope::Repo, repo_config_path(env));

    // Explicit --config always wins; it is the last (most specific) layer.
    if (!env.explicit_path.empty())
        layers.push_back({Scope::Explicit, env.explicit_path, file_exists(env.explicit_path)});

    return layers;
}

}  // namespace keur::config
