/**
 * @file cmd/config.cpp
 * @brief keur config — show the resolved config layer stack (read-only).
 *
 * Orchestrator only: it resolves the layer stack (config/scopes, pure) using
 * the injected Git adapter for the repo root and the Fs adapter for existence,
 * then renders. The rendering functions are pure (layers → strings), so they
 * are unit-tested without any I/O. @implements ADR-003
 */
#include "cmd/config.h"

#include <iostream>

#include "util/keur_utils.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// config_parse
// ---------------------------------------------------------------------------

ConfigOptions config_parse(const std::vector<std::string>& args) {
    ConfigOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") {
            o.json = true;
        } else if (args[i] == "--config" && i + 1 < args.size()) {
            o.config = args[++i];
        } else if (!args[i].empty() && args[i][0] != '-') {
            o.subcommand = args[i];  // positional = subcommand (only "list" now)
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Pure rendering
// ---------------------------------------------------------------------------

std::string config_layer_line(const config::Layer& l) {
    std::string name = config::scope_name(l.scope);
    std::string path = l.path.empty() ? "(built-in)" : l.path;
    std::string state = l.path.empty() ? "" : (l.exists ? "  [present]" : "  [absent]");
    // pad the scope name to a stable column so paths line up.
    std::string pad(name.size() < 10 ? 10 - name.size() : 1, ' ');
    return "  " + name + pad + path + state;
}

std::vector<std::string> config_render_text(const std::vector<config::Layer>& layers) {
    std::vector<std::string> out;
    out.emplace_back("keur config layers (general → specific; later overrides earlier)");
    for (const auto& l : layers) out.emplace_back(config_layer_line(l));
    return out;
}

std::string config_render_json(const std::vector<config::Layer>& layers) {
    std::string s = "[";
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& l = layers[i];
        if (i) s += ",";
        s += "{\"scope\":\"" + jesc(config::scope_name(l.scope)) + "\",";
        s += "\"path\":\"" + jesc(l.path) + "\",";
        s += "\"exists\":";
        s += (l.path.empty() ? "true" : (l.exists ? "true" : "false"));
        s += "}";
    }
    s += "]";
    return s;
}

// ---------------------------------------------------------------------------
// config — orchestrator over the injected adapters
// ---------------------------------------------------------------------------

int config(const ConfigOptions& opts, const ConfigDeps& deps) {
    if (opts.subcommand != "list") {
        std::cerr << "keur config: unknown subcommand '" << opts.subcommand
                  << "' (supported: list)\n";
        return 1;
    }

    // repo root via git (empty when not in a repo). git.run trims output.
    std::string repo_root = deps.git.run("rev-parse --show-toplevel");
    config::ScopeEnv env = config::capture_scope_env(opts.config, repo_root);
    auto layers = config::resolve_layers(
        env, [&deps](const std::string& p) { return deps.fs.exists(p); });

    if (opts.json) {
        std::cout << config_render_json(layers) << "\n";
    } else {
        for (const auto& line : config_render_text(layers)) std::cout << line << "\n";
    }
    return 0;
}

}  // namespace keur::cmd
