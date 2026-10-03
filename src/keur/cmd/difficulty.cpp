/**
 * @file cmd/difficulty.cpp
 * @brief `keur difficulty` — explain the resolved difficulty + why a rule is
 *        (in)active and what to change. @implements DESIGN-DIFFRES.explain
 */
#include "cmd/difficulty.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "config/ruleset.h"
#include "config/scopes.h"
#include "util/keur_utils.h"

namespace keur::cmd {

DifficultyOptions difficulty_parse(const std::vector<std::string>& args) {
    DifficultyOptions o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--explain") o.explain = true;
        else if (a == "--json") o.json = true;
        else if (a == "--why" && i + 1 < args.size()) o.why = args[++i];
        else if (a == "--why-not" && i + 1 < args.size()) o.why_not = args[++i];
    }
    if (o.why.empty() && o.why_not.empty()) o.explain = true;  // default action
    return o;
}

namespace {

// Load the full corpus (no filter) so we can report active vs sleeping.
std::vector<Rule> load_all_rules() {
    std::vector<Rule> rules;
    for (const auto& d : keur::rule_dirs())
        for (auto& r : rules_load(d)) rules.push_back(std::move(r));
    return rules;
}

// Resolve the difficulty with a per-layer trace from the real config stack.
config::DifficultyResolved resolve_now() {
    adapters::Git git{};
    adapters::Fs fs{};
    std::string repo_root = git.run("rev-parse --show-toplevel");
    config::ScopeEnv env = config::capture_scope_env("", repo_root);
    auto layers = config::resolve_layers(env, [&fs](const std::string& p) { return fs.exists(p); });
    std::vector<std::pair<std::string, std::string>> scope_paths;  // global->repo
    for (const auto& ly : layers)
        if (ly.exists && !ly.path.empty())
            scope_paths.emplace_back(config::scope_name(ly.scope), ly.path);
    return config::resolve_difficulty_explained(
        scope_paths,
        [&fs](const std::string& p) { auto rr = fs.read_file(p); return rr.ok ? rr.content : std::string(); },
        /*default_level=*/1);
}

const Rule* find_rule(const std::vector<Rule>& rules, const std::string& id) {
    for (const auto& r : rules) if (r.id == id) return &r;
    return nullptr;
}

// Human-readable forward explain.
void print_explain_human(const config::DifficultyResolved& res, const std::vector<Rule>& all) {
    std::printf("difficulty resolution\n\n  layer stack (later wins):\n");
    std::printf("    default            all ilities = %d\n", res.default_level);
    for (const auto& ly : res.layers) {
        if (ly.level > 0)
            std::printf("    %-8s [policy]  level = %d  -> all = %d\n", ly.scope.c_str(), ly.level, ly.level);
        for (const auto& kv : ly.ilities)
            std::printf("    %-8s [difficulty] %s = %d\n", ly.scope.c_str(), kv.first.c_str(), kv.second);
        for (const auto& kv : ly.rules)
            std::printf("    %-8s [rules] %s = %s\n", ly.scope.c_str(), kv.first.c_str(), kv.second ? "on" : "off");
    }
    std::printf("\n  resolved vector:\n");
    // print explicit ilities + note default for the rest
    for (const auto& kv : res.vector.levels)
        std::printf("    %-18s %s\n", kv.first.c_str(), kv.second == 0 ? "OFF" : std::to_string(kv.second).c_str());
    std::printf("    (every other ility = %d)\n", res.vector.default_level);

    int active = 0;
    std::map<std::string, int> by_ility;
    for (const auto& r : all) {
        auto ov = res.overrides.find(r.id);
        bool on = (ov != res.overrides.end()) ? ov->second : rule_active(r, res.vector);
        if (on) { active++; for (const auto& il : rule_ilities(r)) by_ility[il]++; }
    }
    std::printf("\n  active rules: %d / %zu\n", active, all.size());
    for (const auto& kv : by_ility)
        std::printf("    %-18s %d\n", kv.first.c_str(), kv.second);
    if (!res.overrides.empty()) {
        std::printf("  per-rule overrides:\n");
        for (const auto& kv : res.overrides)
            std::printf("    %s = %s\n", kv.first.c_str(), kv.second ? "on" : "off");
    }
}

void print_explain_json(const config::DifficultyResolved& res, const std::vector<Rule>& all) {
    int active = 0;
    for (const auto& r : all) {
        auto ov = res.overrides.find(r.id);
        bool on = (ov != res.overrides.end()) ? ov->second : rule_active(r, res.vector);
        if (on) active++;
    }
    std::printf("{\"default_level\":%d,\"ilities\":{", res.vector.default_level);
    bool first = true;
    for (const auto& kv : res.vector.levels) {
        std::printf("%s\"%s\":%d", first ? "" : ",", kv.first.c_str(), kv.second);
        first = false;
    }
    std::printf("},\"overrides\":{");
    first = true;
    for (const auto& kv : res.overrides) {
        std::printf("%s\"%s\":%s", first ? "" : ",", kv.first.c_str(), kv.second ? "true" : "false");
        first = false;
    }
    std::printf("},\"active\":%d,\"total\":%zu}\n", active, all.size());
}

// Backward: why is <id> (in)active, and what to change.
int print_why(const config::DifficultyResolved& res, const std::vector<Rule>& all,
              const std::string& id, bool json) {
    const Rule* r = find_rule(all, id);
    if (!r) { std::fprintf(stderr, "unknown rule: %s\n", id.c_str()); return 1; }
    int maturity = r->maturity > 0 ? r->maturity : 3;
    auto ils = rule_ilities(*r);
    auto ov = res.overrides.find(id);
    bool overridden = (ov != res.overrides.end());
    bool active = overridden ? ov->second : rule_active(*r, res.vector);

    // best (max) ility level
    int best = 0; std::string best_il;
    for (const auto& il : ils) { int lvl = res.vector.level_of(il); if (lvl > best) { best = lvl; best_il = il; } }

    if (json) {
        std::printf("{\"rule\":\"%s\",\"active\":%s,\"maturity\":%d,\"ilities\":[",
                    id.c_str(), active ? "true" : "false", maturity);
        for (size_t i = 0; i < ils.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", ils[i].c_str());
        std::printf("],\"best_level\":%d,\"overridden\":%s}\n", best, overridden ? "true" : "false");
        return 0;
    }

    std::printf("%s -> %s\n", id.c_str(), active ? "ACTIVE" : "INACTIVE");
    std::printf("  ilities [");
    for (size_t i = 0; i < ils.size(); ++i) std::printf("%s%s", i ? ", " : "", ils[i].c_str());
    std::printf("]  maturity %d\n", maturity);
    if (overridden) {
        std::printf("  per-rule override: forced %s\n", ov->second ? "ON" : "OFF");
        std::printf("  to CHANGE: remove the [difficulty.rules] %s entry\n", id.c_str());
        return 0;
    }
    if (best == 0)
        std::printf("  all its ilities are OFF -> inactive\n");
    else
        std::printf("  strongest ility %s = %d  %s maturity %d -> %s\n",
                    best_il.c_str(), best, (best >= maturity ? ">=" : "<"), maturity,
                    active ? "active" : "inactive");
    if (!active)
        std::printf("  to ACTIVATE: set one of its ilities >= %d (e.g. %s = %d)\n",
                    maturity, ils.empty() ? "ility" : ils[0].c_str(), maturity);
    else
        std::printf("  to DEACTIVATE: lower all its ilities below %d, or set %s = off in [difficulty.rules]\n",
                    maturity, id.c_str());
    return 0;
}

}  // namespace

int difficulty(const DifficultyOptions& opts) {
    auto all = load_all_rules();
    auto res = resolve_now();

    if (!opts.why.empty())     return print_why(res, all, opts.why, opts.json);
    if (!opts.why_not.empty()) return print_why(res, all, opts.why_not, opts.json);

    if (opts.json) print_explain_json(res, all);
    else           print_explain_human(res, all);
    return 0;
}

}  // namespace keur::cmd
