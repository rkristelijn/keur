/**
 * @file cmd/manager.cpp
 * @brief keur manager --status — aggregate config/hooks/scanners/rules into one
 *        read-only board. Orchestrator only: it gathers via the pure config
 *        modules + injected adapters, then renders. @implements ADR-003
 */
#include "cmd/manager.h"

#include <iostream>
#include <map>
#include <set>

#include "cmd/config.h"
#include "common/platform.h"
#include "rules/rule_engine.h"
#include "util/keur_utils.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// manager_parse
// ---------------------------------------------------------------------------

ManagerOptions manager_parse(const std::vector<std::string>& args) {
    ManagerOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") o.json = true;
        else if (args[i] == "--status") o.subcommand = "status";
        else if (args[i] == "--config" && i + 1 < args.size()) o.config = args[++i];
        else if (!args[i].empty() && args[i][0] != '-') o.subcommand = args[i];
    }
    return o;
}

// ---------------------------------------------------------------------------
// Rule counts per category (unique rule id across resolved layers)
// ---------------------------------------------------------------------------

namespace {

std::vector<RuleCategoryCount> count_rules_by_category(int& total_out) {
    std::map<std::string, std::set<std::string>> ids_by_cat;  // cat -> unique ids
    std::set<std::string> seen;
    for (const auto& dir : rule_dirs())
        for (const auto& r : rules_load(dir)) {
            if (r.id.empty() || seen.count(r.id)) continue;
            seen.insert(r.id);
            ids_by_cat[r.category.empty() ? "(uncategorised)" : r.category].insert(r.id);
        }
    std::vector<RuleCategoryCount> out;
    int total = 0;
    for (const auto& [cat, ids] : ids_by_cat) {
        int n = 0;
        for (auto it = ids.begin(); it != ids.end(); ++it) ++n;  // count as int (no size_t truncation, ADR-006)
        out.push_back({cat, n});
        total += n;
    }
    total_out = total;
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Pure rendering
// ---------------------------------------------------------------------------

std::vector<std::string> manager_render_text(const ManagerBoard& board) {
    std::vector<std::string> out;
    out.emplace_back("keur manager — overview (read-only)");
    out.emplace_back("");

    // Config scopes — reuse the shared per-layer renderer (one renderer, no drift).
    out.emplace_back("Config scopes (general \u2192 specific):");
    for (const auto& l : board.config_layers)
        out.emplace_back(config_layer_line(l));

    // Hooks
    out.emplace_back("");
    out.emplace_back("Hooks:");
    out.emplace_back("  effective owner : " + config::owner_name(board.hooks.owner) +
                     (board.hooks.effective_path.empty()
                          ? ""
                          : "  (core.hooksPath \u2192 " + board.hooks.effective_path + ")"));
    for (const auto& m : board.hooks.managers)
        if (m.detected) out.emplace_back("  detected manager: " + m.name);

    // Scanners
    out.emplace_back("");
    out.emplace_back("Scanners (external evidence tools):");
    for (const auto& s : board.scanners) {
        std::string line = "  " + s.name;
        if (!s.group.empty()) line += "  [" + s.group + "]";
        line += s.enabled ? "  enabled" : "  disabled";
        line += s.on_path ? "  on-PATH" : "  not-on-PATH";
        int present = 0;
        for (const auto& cf : s.config_files) if (cf.present) ++present;
        if (!s.config_files.empty()) {
            const char* sep = "/";  // display "cfg 2/3", not a path. keur:ignore KEUR-PORT-004
            line += "  cfg " + std::to_string(present) + sep +
                    std::to_string(s.config_files.size());
        }
        out.emplace_back(line);
    }

    // Rules
    out.emplace_back("");
    out.emplace_back("Rules (" + std::to_string(board.rule_total) + " total):");
    for (const auto& rc : board.rule_categories)
        out.emplace_back("  " + rc.category + "  " + std::to_string(rc.count));

    return out;
}

std::string manager_render_json(const ManagerBoard& board) {
    std::string s = "{";
    // config layers
    s += "\"config_layers\":[";
    for (std::size_t i = 0; i < board.config_layers.size(); ++i) {
        const auto& l = board.config_layers[i];
        if (i) s += ",";
        s += "{\"scope\":\"" + jesc(config::scope_name(l.scope)) + "\",\"path\":\"" +
             jesc(l.path) + "\",\"exists\":" +
             (l.path.empty() ? "true" : (l.exists ? "true" : "false")) + "}";
    }
    s += "],";
    // hooks
    s += "\"hooks\":{\"owner\":\"" + jesc(config::owner_name(board.hooks.owner)) +
         "\",\"effective_path\":\"" + jesc(board.hooks.effective_path) + "\",\"managers\":[";
    bool first = true;
    for (const auto& m : board.hooks.managers)
        if (m.detected) { if (!first) s += ","; s += "\"" + jesc(m.name) + "\""; first = false; }
    s += "]},";
    // scanners
    s += "\"scanners\":[";
    for (std::size_t i = 0; i < board.scanners.size(); ++i) {
        const auto& sc = board.scanners[i];
        if (i) s += ",";
        s += "{\"name\":\"" + jesc(sc.name) + "\",\"group\":\"" + jesc(sc.group) +
             "\",\"enabled\":" + (sc.enabled ? "true" : "false") +
             ",\"on_path\":" + (sc.on_path ? "true" : "false") + "}";
    }
    s += "],";
    // rules
    s += "\"rules\":{\"total\":" + std::to_string(board.rule_total) + ",\"categories\":[";
    for (std::size_t i = 0; i < board.rule_categories.size(); ++i) {
        if (i) s += ",";
        s += "{\"category\":\"" + jesc(board.rule_categories[i].category) +
             "\",\"count\":" + std::to_string(board.rule_categories[i].count) + "}";
    }
    s += "]}}";
    return s;
}

// ---------------------------------------------------------------------------
// manager — orchestrator
// ---------------------------------------------------------------------------

int manager(const ManagerOptions& opts, const ManagerDeps& deps) {
    if (opts.subcommand != "status") {
        std::cerr << "keur manager: unknown subcommand '" << opts.subcommand
                  << "' (supported: status)\n";
        return 1;
    }

    ManagerBoard board;
    std::string repo_root = deps.git.run("rev-parse --show-toplevel");

    // Config scopes.
    config::ScopeEnv env = config::capture_scope_env(opts.config, repo_root);
    board.config_layers = config::resolve_layers(
        env, [&](const std::string& p) { return deps.fs.exists(p); });

    // Hook situation (+ coexisting managers).
    config::HookProbe hp;
    hp.git_config = [&](const std::string& a) { return deps.git.run(a); };
    hp.exists = [&](const std::string& p) { return deps.fs.exists(p); };
    hp.repo_root = repo_root;
    board.hooks = config::detect_hook_situation(hp);

    // Scanners: parse the most-specific existing config layer, then resolve.
    std::string scanner_toml;
    for (const auto& l : board.config_layers)
        if (!l.path.empty() && l.exists) {
            auto rr = deps.fs.read_file(l.path);
            if (rr.ok) scanner_toml = rr.content;  // later layer overrides earlier
        }
    auto declared = config::parse_scanners(scanner_toml);
    config::ScannerProbe sp;
    sp.tool_on_path = [](const std::string& t) { return keur::platform::tool_on_path(t); };
    sp.exists = [&](const std::string& p) { return deps.fs.exists(p); };
    sp.repo_root = repo_root;
    board.scanners = config::resolve_scanners(declared, sp);

    // Rules by category.
    board.rule_categories = count_rules_by_category(board.rule_total);

    if (opts.json) std::cout << manager_render_json(board) << "\n";
    else for (const auto& line : manager_render_text(board)) std::cout << line << "\n";
    return 0;
}

}  // namespace keur::cmd
