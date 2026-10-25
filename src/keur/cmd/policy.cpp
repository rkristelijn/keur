/**
 * @file cmd/policy.cpp
 * @brief keur policy — explain the resolved policy and thresholds.
 */
#include "cmd/policy.h"
#include "util/keur_utils.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace keur::cmd {

// ---------------------------------------------------------------------------
// Minimal TOML scalar reader — same approach as cpm's toml.cpp but self-
// contained so policy has no external dependencies beyond the STL.
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
// find_config_upward — repo-local discovery
// ---------------------------------------------------------------------------

std::string find_config_upward(const std::string& start_dir) {
    // Walk up from start_dir to the filesystem root, returning the first
    // directory that holds a keur.toml. Portable: treats both '/' and '\' as
    // separators so it works on POSIX and Windows paths.
    std::string dir = start_dir;
    // Normalise a trailing separator or "/." so the first probe hits start_dir.
    auto trim_trailing = [](std::string& p) {
        while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
        if (p.size() >= 2 && p.back() == '.' &&
            (p[p.size() - 2] == '/' || p[p.size() - 2] == '\\')) {
            p.pop_back();
            while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
        }
    };
    trim_trailing(dir);

    while (!dir.empty()) {
        std::string sep = (dir.back() == '/' || dir.back() == '\\') ? "" : "/";
        // Uncluttered-first: a <dir>/.config/keur.toml wins over <dir>/keur.toml,
        // so a project can keep its config out of the root (nodejs/tooling#71).
        std::string dotcfg = dir + sep + ".config" + "/keur.toml";
        std::ifstream df(dotcfg);
        if (df.good()) return dotcfg;
        std::string candidate = dir + sep + "keur.toml";
        std::ifstream f(candidate);
        if (f.good()) return candidate;

        // Move to the parent directory.
        auto slash = dir.find_last_of("/\\");
        if (slash == std::string::npos) break;      // no more parents
        if (slash == 0) { dir = dir.substr(0, 1); } // POSIX root "/"
        else dir = dir.substr(0, slash);
        // Stop once we have probed the root itself.
        if (dir == "/" ) {
            std::ifstream rf("/keur.toml");
            if (rf.good()) return "/keur.toml";
            break;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// resolve_config
// ---------------------------------------------------------------------------

std::string resolve_config(const std::string& hint) {
    // 1. Explicit hint from --config flag
    if (!hint.empty()) return hint;

    // 2. Repo-local: a keur.toml found by walking up from the current directory.
    //    This lets a project set its own policy (ADR-011) and have the hook
    //    honour it, without --config. Repo-local wins over the global config.
    if (const char* cwd = std::getenv("PWD"); cwd && *cwd) {
        std::string local = find_config_upward(cwd);
        if (!local.empty()) return local;
    }

    // 3. Global: $XDG_CONFIG_HOME/keur/keur.toml
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string global_dir = xdg ? std::string(xdg) : (std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.config");
    std::string global_cfg = global_dir + "/keur/keur.toml";
    {
        std::ifstream f(global_cfg);
        if (f.good()) return global_cfg;
    }

    // 4. KEUR_HOME (installed location) — $KEUR_HOME if set, else derived from
    //    the binary location (ADR-063), so the installed config resolves without
    //    a required env var.
    std::string keur_home = keur::derive_keur_home();
    if (!keur_home.empty()) {
        std::string installed = keur_home + "/defaults/keur.toml";
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
        // A malformed value in a user config must not crash the gate. Parse it;
        // on error keep the safe default that is already in `s`.
        try {
            s.block_at = std::stod(block);
        } catch (const std::exception&) {
            std::cerr << "keur: ignoring malformed block-at in " << config_path << "\n";
        }
    }

    auto warn = toml_scalar(config_path, "warn-at");
    if (!warn.empty()) {
        try {
            s.warn_at = std::stod(warn);
        } catch (const std::exception&) {
            std::cerr << "keur: ignoring malformed warn-at in " << config_path << "\n";
        }
    }

    s.version = toml_scalar(config_path, "version");
    return s;
}

// ---------------------------------------------------------------------------
// policy_parse
// ---------------------------------------------------------------------------

PolicyOptions policy_parse(const std::vector<std::string>& args) {
    PolicyOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--config" && i + 1 < args.size()) {
            o.config = args[++i];
        } else if (args[i][0] != '-') {
            // Any positional token is the subcommand (validated in policy).
            o.subcommand = args[i];
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// policy
// ---------------------------------------------------------------------------

int policy(const PolicyOptions& opts) {
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

}  // namespace keur::cmd
