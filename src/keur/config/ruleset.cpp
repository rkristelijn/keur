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
    auto layers = resolve_layers(env, [&](const std::string& p) { return fs.exists(p); });
    auto disabled = resolve_disabled(layers, [&](const std::string& p) {
        auto rr = fs.read_file(p);
        return rr.ok ? rr.content : std::string();
    });

    if (out_disabled) *out_disabled = disabled;
    return filter_disabled(std::move(rules), disabled);
}

}  // namespace keur::config
