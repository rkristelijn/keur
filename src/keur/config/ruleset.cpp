/**
 * @file config/ruleset.cpp
 * @brief The active rule set = loaded rules minus the disable-list. The filter
 *        is pure; load_active_rules does the I/O (dirs + config) at the edge.
 *        @implements KEUR-DISABLED
 */
#include "config/ruleset.h"

#include <fstream>
#include <set>
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
    // Back-compat: a scalar level is the all-equal vector (DESIGN-DIFFRES.compat).
    // Reuse the one code path so there is no second filter semantics.
    DifficultyVector vec;
    vec.default_level = level;
    (void)default_maturity;  // rule_active applies ADR-026 default (3) itself
    return filter_by_vector(std::move(rules), vec, {});
}

namespace {
// Shared TOML line scanner: iterate trimmed, comment-stripped lines, tracking
// the current [section]. Mirrors policy_level's style (dependency-free).
template <typename Fn>
void for_each_toml_line(const std::string& body, Fn&& fn) {
    auto trim = [](std::string s) {
        auto a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        auto b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    std::string section;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        std::string line = trim(nl == std::string::npos ? body.substr(pos)
                                                        : body.substr(pos, nl - pos));
        pos = (nl == std::string::npos) ? body.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = line; continue; }
        fn(section, line);
    }
}

// Strip an enclosing pair of quotes from a value.
std::string unquote(std::string v) {
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
        return v.substr(1, v.size() - 2);
    return v;
}
}  // namespace

void parse_difficulty(const std::string& toml_body, DifficultyVector& out_vec,
                      std::map<std::string, bool>& out_overrides) {
    for_each_toml_line(toml_body, [&out_vec, &out_overrides](const std::string& section, const std::string& line) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string s) {
            auto a = s.find_first_not_of(" \t\r");
            if (a == std::string::npos) return std::string();
            auto b = s.find_last_not_of(" \t\r");
            return s.substr(a, b - a + 1);
        };
        key = trim(key); val = unquote(trim(val));
        if (section == "[difficulty]") {
            int lvl;
            if (val == "off") lvl = 0;
            else { try { lvl = std::stoi(val); } catch (...) { return; } }
            if (lvl < 0) lvl = 0; if (lvl > 5) lvl = 5;  // clamp (DESIGN-DIFFRES.validate)
            // upsert into the vector's level map
            bool found = false;
            for (auto& kv : out_vec.levels) if (kv.first == key) { kv.second = lvl; found = true; break; }
            if (!found) out_vec.levels.push_back({key, lvl});
        } else if (section == "[difficulty.rules]") {
            if (val == "on") out_overrides[key] = true;
            else if (val == "off") out_overrides[key] = false;
        }
    });
}

DifficultyVector merge_difficulty(
    const std::vector<std::string>& layer_paths_global_to_repo,
    const std::function<std::string(const std::string&)>& read,
    std::map<std::string, bool>& out_overrides, int default_level) {
    DifficultyVector vec;
    vec.default_level = default_level;
    for (const auto& path : layer_paths_global_to_repo) {
        if (path.empty()) continue;
        std::string body = read(path);
        if (body.empty()) continue;
        // [policy] level is the all-equal shorthand: it raises default_level so
        // every ility inherits it unless [difficulty] overrides per key.
        int lvl = policy_level(body);
        if (lvl > 0) vec.default_level = lvl;
        parse_difficulty(body, vec, out_overrides);
    }
    return vec;
}

std::vector<Rule> filter_by_vector(std::vector<Rule> rules, const DifficultyVector& vec,
                                   const std::map<std::string, bool>& overrides) {
    std::vector<Rule> kept;
    kept.reserve(rules.size());
    for (auto& r : rules) {
        auto ov = overrides.find(r.id);
        bool active = (ov != overrides.end()) ? ov->second : rule_active(r, vec);
        if (active) kept.push_back(std::move(r));
    }
    return kept;
}

DifficultyResolved resolve_difficulty_explained(
    const std::vector<std::pair<std::string, std::string>>& scope_paths_global_to_repo,
    const std::function<std::string(const std::string&)>& read, int default_level) {
    DifficultyResolved out;
    out.default_level = default_level;
    out.vector.default_level = default_level;
    for (const auto& [scope, path] : scope_paths_global_to_repo) {
        if (path.empty()) continue;
        std::string body = read(path);
        if (body.empty()) continue;
        DifficultyLayer layer;
        layer.scope = scope;
        layer.path = path;
        layer.level = policy_level(body);
        if (layer.level > 0) {
            out.vector.default_level = layer.level;  // all-equal shorthand raises default
        }
        // Capture what THIS layer sets (for the trace), while also folding it in.
        DifficultyVector before = out.vector;
        parse_difficulty(body, out.vector, out.overrides);
        // Diff: which ilities did this layer newly set/change?
        for (const auto& kv : out.vector.levels) {
            int prev = -999;
            for (const auto& b : before.levels) if (b.first == kv.first) { prev = b.second; break; }
            if (prev != kv.second) layer.ilities.push_back({kv.first, kv.second});
        }
        // Rule overrides present in this body.
        DifficultyVector dummy;
        std::map<std::string, bool> this_ov;
        parse_difficulty(body, dummy, this_ov);
        for (const auto& kv : this_ov) layer.rules.push_back({kv.first, kv.second});
        out.layers.push_back(std::move(layer));
    }
    return out;
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
    // 1. gather every rule from the resolved rule dirs, de-duplicating by id.
    //    The same rule id can appear in more than one dir (e.g. the repo's
    //    rules/ AND the installed ~/.config/keur/rules snapshot — ADR-051). The
    //    dirs are in precedence order (built-in -> global -> personal -> project
    //    -> KEUR_RULES_DIRS), so the FIRST occurrence of an id wins and later
    //    copies are skipped. Without this, an overlapping id would be loaded and
    //    evaluated twice, emitting a duplicate finding for the same issue.
    std::vector<Rule> rules;
    std::set<std::string> seen_ids;
    for (const auto& d : keur::rule_dirs())
        for (auto& r : rules_load(d))
            if (seen_ids.insert(r.id).second) rules.push_back(std::move(r));

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
    }, repo_root, env.home);

    if (out_disabled) *out_disabled = disabled;
    auto kept = filter_disabled(std::move(rules), disabled);

    // 3. Difficulty resolution (DESIGN-DIFFRES): fold the config layer stack into
    //    one DifficultyVector (global -> repo, [policy] level as all-equal
    //    shorthand + [difficulty] per ility + [difficulty.rules] overrides), then
    //    keep only the rules active under it (ADR-039 OR/max). Resolve ONCE here,
    //    before the scan — never per file (DESIGN-DIFFRES.perf). Absent config ->
    //    default_level 1 (CMMI-1): only maturity-1 rules active (ADR-026/011).
    std::vector<std::string> layer_paths;  // global -> repo order for the cascade
    for (const auto& ly : layers)
        if (ly.exists && !ly.path.empty()) layer_paths.push_back(ly.path);
    std::map<std::string, bool> overrides;
    // Start from default_level 0 = "no filter". A [policy] level or [difficulty]
    // key in any layer raises it; only THEN does the maturity/ility filter apply.
    // Rationale (backward-compat): a repo with no difficulty config keeps the old
    // behaviour (all rules active). CMMI-1 is a deliberate choice (keur level 1),
    // not a silent default that would make a fresh repo suddenly detect almost
    // nothing. @see DESIGN-DIFFRES.compat
    auto vec = merge_difficulty(
        layer_paths,
        [&fs](const std::string& p) { auto rr = fs.read_file(p); return rr.ok ? rr.content : std::string(); },
        overrides, /*default_level=*/0);
    // No difficulty signal anywhere -> skip the filter entirely (keep all).
    if (vec.default_level == 0 && vec.levels.empty() && overrides.empty())
        return kept;
    kept = filter_by_vector(std::move(kept), vec, overrides);
    return kept;
}

}  // namespace keur::config
