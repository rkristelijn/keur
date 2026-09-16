/**
 * @file cmd_policy.cpp
 * @brief keur policy — explain the resolved policy and thresholds.
 */
#include "cmd/cmd_policy.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace keur {

// ---------------------------------------------------------------------------
// Minimal TOML scalar reader — same approach as cpm's toml.cpp but self-
// contained so cmd_policy has no external dependencies beyond the STL.
// ---------------------------------------------------------------------------

static std::string toml_scalar(const std::string& path, const std::string& key) {
    std::ifstream f(path);
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        // Match lines like: key = "value"  or  key = 0.85
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string lhs = line.substr(0, eq);
        // Trim lhs whitespace
        auto s = lhs.find_first_not_of(" \t");
        auto e = lhs.find_last_not_of(" \t");
        if (s == std::string::npos) continue;
        lhs = lhs.substr(s, e - s + 1);
        if (lhs != key) continue;
        std::string rhs = line.substr(eq + 1);
        s = rhs.find_first_not_of(" \t");
        if (s == std::string::npos) return "";
        rhs = rhs.substr(s);
        // Strip optional surrounding quotes
        if (!rhs.empty() && rhs.front() == '"') {
            rhs = rhs.substr(1);
            auto qe = rhs.find('"');
            if (qe != std::string::npos) rhs = rhs.substr(0, qe);
        } else {
            // Remove inline comment and trailing whitespace
            auto c = rhs.find('#');
            if (c != std::string::npos) rhs = rhs.substr(0, c);
            while (!rhs.empty() && (rhs.back() == ' ' || rhs.back() == '\t' ||
                                    rhs.back() == '\r'))
                rhs.pop_back();
        }
        return rhs;
    }
    return "";
}

// ---------------------------------------------------------------------------
// resolve_config
// ---------------------------------------------------------------------------

std::string resolve_config(const std::string& hint) {
    // 1. Explicit hint from --config flag
    if (!hint.empty()) return hint;

    // 2. Global: $XDG_CONFIG_HOME/keur/keur.toml
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string global_dir = xdg ? std::string(xdg) : (std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.config");
    std::string global_cfg = global_dir + "/keur/keur.toml";
    {
        std::ifstream f(global_cfg);
        if (f.good()) return global_cfg;
    }

    // 3. KEUR_HOME (installed location)
    const char* keur_home = std::getenv("KEUR_HOME");
    if (keur_home) {
        std::string installed = std::string(keur_home) + "/config/keur.toml";
        std::ifstream f(installed);
        if (f.good()) return installed;
    }

    return global_cfg;  // fallback placeholder
}

// ---------------------------------------------------------------------------
// policy_load
// ---------------------------------------------------------------------------

PolicySettings policy_load(const std::string& config_path) {
    PolicySettings s;
    s.config_path = config_path;

    auto block = toml_scalar(config_path, "block-at");
    if (!block.empty()) {
        try { s.block_at = std::stod(block); } catch (...) {}
    }

    auto warn = toml_scalar(config_path, "warn-at");
    if (!warn.empty()) {
        try { s.warn_at = std::stod(warn); } catch (...) {}
    }

    s.version = toml_scalar(config_path, "version");
    return s;
}

// ---------------------------------------------------------------------------
// cmd_policy_parse
// ---------------------------------------------------------------------------

PolicyOptions cmd_policy_parse(const std::vector<std::string>& args) {
    PolicyOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--config" && i + 1 < args.size()) {
            o.config = args[++i];
        } else if (args[i][0] != '-') {
            // Any positional token is the subcommand (validated in cmd_policy).
            o.subcommand = args[i];
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// cmd_policy
// ---------------------------------------------------------------------------

int cmd_policy(const PolicyOptions& opts) {
    if (opts.subcommand != "explain") {
        std::cerr << "keur policy: unknown subcommand '" << opts.subcommand
                  << "' (supported: explain)\n";
        return 1;
    }

    auto cfg    = resolve_config(opts.config);
    auto policy = policy_load(cfg);

    std::cout << "keur policy (resolved from: " << cfg << ")\n"
              << "\n"
              << "  version  : " << (policy.version.empty() ? "(unset)" : policy.version) << "\n"
              << "  block-at : " << policy.block_at
              << "  (findings with action=block and confidence >= this value block the commit)\n"
              << "  warn-at  : " << policy.warn_at
              << "  (findings at or above this confidence level emit a warning)\n"
              << "\n"
              << "To override: set block-at / warn-at in " << cfg << "\n";

    return 0;
}

}  // namespace keur
