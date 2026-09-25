/**
 * @file config/ruleset.cpp
 * @brief The active rule set = loaded rules minus the disable-list. The filter
 *        is pure; load_active_rules does the I/O (dirs + config) at the edge.
 *        @implements KEUR-DISABLED
 */
#include "config/ruleset.h"

#include <fstream>
#include <sstream>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "config/disabled.h"
#include "config/scopes.h"
#include "util/keur_utils.h"

namespace keur::config {

std::vector<Rule> filter_disabled(std::vector<Rule> rules,
                                  const std::set<std::string>& disabled) {
    if (disabled.empty()) return rules;
    std::vector<Rule> kept;
    kept.reserve(rules.size());
    for (auto& r : rules)
        if (!disabled.count(r.id)) kept.push_back(std::move(r));
    return kept;
}

std::vector<Rule> filter_by_maturity(std::vector<Rule> rules, int level,
                                     int default_maturity) {
    if (level <= 0) return rules;  // no level configured -> no maturity filter
    std::vector<Rule> kept;
    kept.reserve(rules.size());
    for (auto& r : rules) {
        int m = r.maturity > 0 ? r.maturity : default_maturity;
        if (m <= level) kept.push_back(std::move(r));
    }
    return kept;
}

int policy_level(const std::string& toml_body) {
    // Line scanner: find [policy], then its `level = N` (mirrors upsert in
    // cmd/level.cpp). Small + dependency-free, like the other config readers.
    bool in_policy = false;
    size_t pos = 0;
    auto trim = [](std::string s) {
        auto a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        auto b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    while (pos < toml_body.size()) {
        size_t nl = toml_body.find('\n', pos);
        std::string line = trim(nl == std::string::npos ? toml_body.substr(pos)
                                                        : toml_body.substr(pos, nl - pos));
        pos = (nl == std::string::npos) ? toml_body.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { in_policy = (line == "[policy]"); continue; }
        if (in_policy && line.rfind("level", 0) == 0) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                try { return std::stoi(trim(line.substr(eq + 1))); }
                catch (const std::exception&) { return 0; }
            }
        }
    }
    return 0;
}

std::vector<Rule> load_active_rules(std::set<std::string>* out_disabled) {
    // 1. gather every rule from the resolved rule dirs.
    std::vector<Rule> rules;
    for (const auto& d : keur::rule_dirs())
        for (auto& r : rules_load(d)) rules.push_back(std::move(r));

    // 2. resolve the disable-list from the config layer stack. I/O via the fs
    //    adapter (existence) + a plain read here; git gives the repo root.
    adapters::Git git{};
    adapters::Fs fs{};
    std::string repo_root = git.run("rev-parse --show-toplevel");
    ScopeEnv env = capture_scope_env("", repo_root);
    auto layers = resolve_layers(env, [&fs](const std::string& p) { return fs.exists(p); });
    auto disabled = resolve_disabled(layers, [&fs](const std::string& p) {
        auto rr = fs.read_file(p);
        return rr.ok ? rr.content : std::string();
    });

    if (out_disabled) *out_disabled = disabled;
    auto kept = filter_disabled(std::move(rules), disabled);

    // 3. Maturity band (ADR-026): keep only rules that gate at or below the
    //    project's chosen level ([policy] level, written by `keur level`).
    //    Read from the resolved layer stack, most-specific-first (repo/explicit
    //    win over global). Absent -> level 0 -> no filter (backwards compatible).
    int level = 0;
    for (auto it = layers.rbegin(); it != layers.rend() && level == 0; ++it) {
        if (!it->exists || it->path.empty()) continue;
        auto rr = fs.read_file(it->path);
        if (rr.ok) level = policy_level(rr.content);
    }
    kept = filter_by_maturity(std::move(kept), level);
    return kept;
}

}  // namespace keur::config
