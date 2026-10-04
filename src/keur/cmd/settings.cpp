/**
 * @file cmd/settings.cpp
 * @brief keur settings — per-ility sliders grouped by ISO 25010 (see settings.h).
 *        Pure model + reducers + renderer here; the I/O edge (raw mode, keys,
 *        file write) mirrors cmd/level.cpp and routes through platform + Fs.
 * @implements ADR-038
 * @implements ADR-039
 */
#include "cmd/settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "cmd/policy.h"
#include "common/platform.h"
#include "config/ruleset.h"
#include "config/scopes.h"
#include "util/keur_utils.h"

namespace keur::cmd {

namespace {

// DOOM-menu red (match cmd/level.cpp so the views feel like one screen).
constexpr const char* k_red     = "\x1b""[38;5;196m";
constexpr const char* k_red_dim = "\x1b""[38;5;124m";
constexpr const char* k_dim     = "\x1b""[2m";
constexpr const char* k_reset   = "\x1b""[0m";

std::string paint(const std::string& s, const char* code, bool color) {
    if (!color) return s;
    return std::string(code) + s + k_reset;
}

constexpr int k_max_level = 5;  // off(0) .. 5

// Row/column counts are size_t; the model's cursor is an int (part of the
// public struct). Convert through this helper so the one narrowing conversion
// is explicit and bounded, and the cast never sits directly on a .size() call
// site (the word-size portability control, KEUR-PORT-006 / ADR-006).
template <typename T>
int isize(const std::vector<T>& v) {
    const std::size_t n = v.size();
    return static_cast<int>(n);
}

}  // namespace

// ---------------------------------------------------------------------------
// iso25010_groups — the fixed ISO 25010 characteristic -> ility map.
// Order + membership follow ADR-039 §1 / rule/resolve.cpp's fallback groups.
// @id KEUR-SETTINGS.iso25010
// ---------------------------------------------------------------------------
const std::vector<IsoGroup>& iso25010_groups() {
    static const std::vector<IsoGroup> kGroups = {
        {"Security",        {"confidentiality", "integrity", "authenticity"}},
        {"Reliability",     {"reliability-maturity", "fault-tolerance", "availability"}},
        {"Maintainability", {"analysability", "modifiability", "testability", "modularity"}},
        {"Performance",     {"time-behaviour"}},
        {"Usability",       {"accessibility"}},
        {"Portability",     {"adaptability"}},
        {"Process",         {"traceability"}},
    };
    return kGroups;
}

// ---------------------------------------------------------------------------
// build_settings_model — resolved vector -> tree. @id KEUR-SETTINGS.build
// ---------------------------------------------------------------------------
SettingsModel build_settings_model(const DifficultyVector& vec,
                                   const std::vector<std::string>& overridden_ilities) {
    SettingsModel m;
    for (const auto& g : iso25010_groups()) {
        GroupNode gn;
        gn.name = g.name;
        gn.expanded = false;
        for (const auto& il : g.ilities) {
            SliderNode s;
            s.ility = il;
            s.level = vec.level_of(il);  // 0 == OFF
            if (s.level < 0) s.level = 0;
            if (s.level > k_max_level) s.level = k_max_level;
            s.has_override = std::find(overridden_ilities.begin(),
                                       overridden_ilities.end(), il)
                             != overridden_ilities.end();
            gn.sliders.push_back(std::move(s));
        }
        m.groups.push_back(std::move(gn));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Queries. @id KEUR-SETTINGS.rows / .average / .star
// ---------------------------------------------------------------------------
std::vector<SettingsRow> settings_visible_rows(const SettingsModel& m) {
    std::vector<SettingsRow> rows;
    for (int gi = 0; gi < isize(m.groups); ++gi) {
        SettingsRow hdr;
        hdr.group = gi;
        hdr.slider = -1;
        rows.push_back(hdr);
        if (m.groups[static_cast<std::size_t>(gi)].expanded) {
            const auto& g = m.groups[static_cast<std::size_t>(gi)];
            for (int si = 0; si < isize(g.sliders); ++si) {
                SettingsRow r;
                r.group = gi;
                r.slider = si;
                rows.push_back(r);
            }
        }
    }
    return rows;
}

double group_average(const GroupNode& g) {
    int sum = 0, n = 0;
    for (const auto& s : g.sliders) {
        if (s.level > 0) { sum += s.level; ++n; }
    }
    if (n == 0) return 0.0;
    double avg = static_cast<double>(sum) / n;
    return std::round(avg * 10.0) / 10.0;  // one decimal
}

bool group_has_star(const GroupNode& g) {
    // Override on any slider -> star.
    for (const auto& s : g.sliders)
        if (s.has_override) return true;
    // Non-OFF ilities not all equal -> star.
    int first = -1;
    for (const auto& s : g.sliders) {
        if (s.level == 0) continue;
        if (first < 0) first = s.level;
        else if (s.level != first) return true;
    }
    return false;
}

bool model_has_star(const SettingsModel& m) {
    for (const auto& g : m.groups)
        if (group_has_star(g)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Reducers. @id KEUR-SETTINGS.move / .expand / .collapse / .adjust
// ---------------------------------------------------------------------------
void settings_move(SettingsModel& m, int delta) {
    const int n = isize(settings_visible_rows(m));
    if (n == 0) { m.cursor = 0; return; }
    int c = m.cursor + delta;
    if (c < 0) c = 0;
    if (c > n - 1) c = n - 1;
    m.cursor = c;
}

bool settings_expand(SettingsModel& m) {
    auto rows = settings_visible_rows(m);
    if (m.cursor < 0 || m.cursor >= isize(rows)) return false;
    const SettingsRow& r = rows[static_cast<std::size_t>(m.cursor)];
    if (!r.is_group()) return false;  // sliders don't expand
    GroupNode& g = m.groups[static_cast<std::size_t>(r.group)];
    if (g.expanded) return false;
    g.expanded = true;
    return true;
}

bool settings_collapse(SettingsModel& m) {
    auto rows = settings_visible_rows(m);
    if (m.cursor < 0 || m.cursor >= isize(rows)) return false;
    const SettingsRow& r = rows[static_cast<std::size_t>(m.cursor)];
    int gi = r.group;
    GroupNode& g = m.groups[static_cast<std::size_t>(gi)];
    if (!r.is_group()) {
        // On a slider: collapse the owning group, jump cursor to its header.
        g.expanded = false;
        auto after = settings_visible_rows(m);
        for (int i = 0; i < isize(after); ++i) {
            if (after[static_cast<std::size_t>(i)].group == gi &&
                after[static_cast<std::size_t>(i)].is_group()) {
                m.cursor = i;
                break;
            }
        }
        return true;
    }
    if (!g.expanded) return false;
    g.expanded = false;
    return true;
}

bool settings_adjust(SettingsModel& m, int delta) {
    auto rows = settings_visible_rows(m);
    if (m.cursor < 0 || m.cursor >= isize(rows)) return false;
    const SettingsRow& r = rows[static_cast<std::size_t>(m.cursor)];
    GroupNode& g = m.groups[static_cast<std::size_t>(r.group)];

    auto clamp = [](int v) {
        if (v < 0) return 0;
        if (v > k_max_level) return k_max_level;
        return v;
    };

    if (r.is_group()) {
        // Drag the group: set ALL sliders to the same new level. Base it on the
        // rounded average so a mixed group collapses to one coherent level.
        double avg = group_average(g);
        int base = (avg <= 0.0) ? 0 : static_cast<int>(std::lround(avg));
        int target = clamp(base + delta);
        bool changed = false;
        for (auto& s : g.sliders) {
            if (s.level != target) { s.level = target; changed = true; }
        }
        if (changed) m.dirty = true;
        return changed;
    }

    SliderNode& s = g.sliders[static_cast<std::size_t>(r.slider)];
    int nv = clamp(s.level + delta);
    if (nv == s.level) return false;
    s.level = nv;
    m.dirty = true;
    return true;
}

std::vector<std::pair<std::string, int>> settings_vector(const SettingsModel& m) {
    std::vector<std::pair<std::string, int>> out;
    for (const auto& g : m.groups)
        for (const auto& s : g.sliders)
            out.push_back({s.ility, s.level});
    return out;
}

// ---------------------------------------------------------------------------
// render_settings_screen — pure. @id KEUR-SETTINGS.render
// Layout per the design doc:
//   Overall  <bar>  <avg> *
//   > Security          <bar>  4 *     [+]        (collapsed group)
//       confidentiality <bar>  4   Security       (expanded slider)
// Bars: 10 cells. OFF renders a detached leftmost stop "· " then an empty bar.
// ---------------------------------------------------------------------------
namespace {

// A 10-cell bar for level 1..5 (each level = 2 filled cells). Level 0 (OFF) is
// a detached dot + empty bar, visually "not on the scale".
std::string level_bar(int level) {
    const int cells = 10;
    if (level <= 0) {
        // detached off marker + empty track
        std::string bar = "\xC2\xB7";  // "·"
        for (int i = 0; i < cells - 1; ++i) bar += "\xE2\x96\x91";  // light shade "░"
        return bar;
    }
    int filled = level * 2;  // 1->2 .. 5->10
    if (filled > cells) filled = cells;
    std::string bar;
    for (int i = 0; i < filled; ++i) bar += "\xE2\x96\x88";       // full block "█"
    for (int i = filled; i < cells; ++i) bar += "\xE2\x96\x91";   // light shade "░"
    return bar;
}

std::string level_text(int level) {
    return level <= 0 ? "off" : std::to_string(level);
}

std::string avg_text(double avg) {
    if (avg <= 0.0) return "off";
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(1);
    os << avg;
    return os.str();
}

}  // namespace

std::string render_settings_screen(const SettingsModel& m, bool color, bool show_hint) {
    std::ostringstream os;
    os << paint("Difficulty settings:", k_red, color) << "\n\n";

    // Overall line = average of all non-OFF ilities across all groups.
    int sum = 0, n = 0;
    for (const auto& g : m.groups)
        for (const auto& s : g.sliders)
            if (s.level > 0) { sum += s.level; ++n; }
    double overall = (n == 0) ? 0.0 : std::round((static_cast<double>(sum) / n) * 10.0) / 10.0;
    {
        std::ostringstream line;
        int ov = (overall <= 0.0) ? 0 : static_cast<int>(std::lround(overall));
        line << "  Overall  " << level_bar(ov) << "  " << avg_text(overall);
        if (model_has_star(m)) line << " *";
        os << paint(line.str(), k_red, color) << "\n\n";
    }

    auto rows = settings_visible_rows(m);
    for (int i = 0; i < isize(rows); ++i) {
        const SettingsRow& r = rows[static_cast<std::size_t>(i)];
        const GroupNode& g = m.groups[static_cast<std::size_t>(r.group)];
        bool sel = (i == m.cursor);
        std::string marker = sel ? "> " : "  ";

        if (r.is_group()) {
            double avg = group_average(g);
            int ov = (avg <= 0.0) ? 0 : static_cast<int>(std::lround(avg));
            std::ostringstream line;
            std::string arrow = g.expanded ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ";  // ▾ / ▸
            // pad group name to a column
            std::string name = g.name;
            while (name.size() < 16) name += " ";
            line << marker << arrow << name << level_bar(ov) << "  " << avg_text(avg);
            if (group_has_star(g)) line << " *";
            os << paint(line.str(), sel ? k_red : k_red_dim, color) << "\n";
        } else {
            const SliderNode& s = g.sliders[static_cast<std::size_t>(r.slider)];
            std::ostringstream line;
            std::string name = s.ility;
            while (name.size() < 20) name += " ";
            line << marker << "    " << name << level_bar(s.level)
                 << "  " << level_text(s.level);
            if (s.has_override) line << "  *";
            os << paint(line.str(), sel ? k_red : k_dim, color) << "\n";
        }
    }

    if (show_hint) {
        os << "\n";
        os << paint("  arrows/jk move · right expand · left collapse · -/+ adjust · s save · q quit",
                    k_dim, color)
           << "\n";
    }
    return os.str();
}

// ---------------------------------------------------------------------------
// upsert_difficulty — pure TOML round-trip. @id KEUR-SETTINGS.persist
// Mirrors upsert_policy_level (cmd/level.cpp): line-oriented, preserves
// unrelated lines, replaces/add keys under [difficulty].
// ---------------------------------------------------------------------------
std::string upsert_difficulty(const std::string& toml_body,
                              const std::vector<std::pair<std::string, int>>& ilities) {
    // Split into lines (preserve content; we re-join with '\n').
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : toml_body) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    auto trim = [](const std::string& s) {
        auto a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        auto b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };

    auto value_of = [](int level) -> std::string {
        return level <= 0 ? "\"off\"" : std::to_string(level);
    };

    // Which keys still need writing (consumed as we overwrite in place).
    std::vector<std::pair<std::string, int>> pending = ilities;
    auto take = [&pending](const std::string& key, std::string& out) -> bool {
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            if (it->first == key) {
                out = key + " = " + (it->second <= 0 ? "\"off\"" : std::to_string(it->second));
                pending.erase(it);
                return true;
            }
        }
        return false;
    };

    std::vector<std::string> out;
    bool in_diff = false, saw_diff = false;

    auto flush_pending = [&]() {
        // Append remaining keys at the end of the [difficulty] section.
        for (const auto& kv : pending)
            out.push_back(kv.first + " = " + value_of(kv.second));
        pending.clear();
    };

    for (const auto& raw : lines) {
        std::string t = trim(raw);
        bool is_section = (t.size() >= 2 && t.front() == '[' && t.back() == ']');
        if (is_section) {
            if (in_diff) flush_pending();  // leaving [difficulty]: write the rest
            // NOTE: a [difficulty.rules] subsection is NOT [difficulty]; treat
            // only the exact [difficulty] table as ours.
            in_diff = (t == "[difficulty]");
            if (in_diff) saw_diff = true;
            out.push_back(raw);
            continue;
        }
        if (in_diff) {
            // Is this an ility line we manage? key = ...
            auto eq = t.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(t.substr(0, eq));
                std::string replacement;
                if (take(key, replacement)) {
                    out.push_back(replacement);
                    continue;
                }
            }
        }
        out.push_back(raw);
    }
    if (in_diff) flush_pending();

    std::string result;
    for (const auto& l : out) { result += l; result += "\n"; }

    if (!saw_diff && !pending.empty()) {
        if (!result.empty() && result.back() != '\n') result += "\n";
        result += "\n[difficulty]\n";
        for (const auto& kv : pending)
            result += kv.first + " = " + value_of(kv.second) + "\n";
    }
    return result;
}

// ---------------------------------------------------------------------------
// settings_parse — pure argv parsing. @id KEUR-SETTINGS.parse
// ---------------------------------------------------------------------------
SettingsOptions settings_parse(const std::vector<std::string>& args) {
    SettingsOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--no-color") o.no_color = true;
        else if (a == "--global") o.global = true;
        else if (a == "--print") o.print_only = true;
        else if (a == "--config" && i + 1 < args.size()) o.config = args[++i];
    }
    return o;
}

// ---------------------------------------------------------------------------
// I/O edge: resolve + build the model, then the interactive loop. Mirrors
// cmd/level.cpp's structure (target_config / RawModeGuard / read_key).
// ---------------------------------------------------------------------------
namespace {

bool color_enabled(const SettingsOptions& opts) {
    if (opts.no_color) return false;
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return platform::terminal_size().cols > 0;
}

std::string target_config(const SettingsOptions& opts) {
    if (opts.global) {
        adapters::Git git{};
        config::ScopeEnv env =
            config::capture_scope_env(opts.config, git.run("rev-parse --show-toplevel"));
        std::string g = config::global_config_path(env);
        if (!g.empty()) return g;
    }
    return resolve_config(opts.config);
}

}  // namespace

// Resolve the current difficulty vector + the ilities carrying a per-rule
// override, so the model opens on the real on-disk state. @id KEUR-SETTINGS.load
SettingsModel load_settings_model() {
    adapters::Git git{};
    adapters::Fs fs{};
    std::string repo_root = git.run("rev-parse --show-toplevel");
    config::ScopeEnv env = config::capture_scope_env("", repo_root);
    auto layers = config::resolve_layers(
        env, [&fs](const std::string& p) { return fs.exists(p); });
    std::vector<std::pair<std::string, std::string>> scope_paths;
    for (const auto& ly : layers)
        if (ly.exists && !ly.path.empty())
            scope_paths.emplace_back(config::scope_name(ly.scope), ly.path);
    auto res = config::resolve_difficulty_explained(
        scope_paths,
        [&fs](const std::string& p) {
            auto rr = fs.read_file(p);
            return rr.ok ? rr.content : std::string();
        },
        /*default_level=*/1);

    // Map per-rule overrides to the ilities they touch (so the slider shows *).
    std::vector<std::string> overridden_ilities;
    if (!res.overrides.empty()) {
        for (const auto& d : keur::rule_dirs()) {
            for (auto& r : rules_load(d)) {
                if (res.overrides.count(r.id)) {
                    for (const auto& il : rule_ilities(r))
                        if (std::find(overridden_ilities.begin(), overridden_ilities.end(), il)
                            == overridden_ilities.end())
                            overridden_ilities.push_back(il);
                }
            }
        }
    }
    return build_settings_model(res.vector, overridden_ilities);
}

// Persist the model's vector to the target keur.toml, keeping one .bak slot
// (same contract as the manager/level writers). @id KEUR-SETTINGS.save
bool save_settings_model(const SettingsOptions& opts, const SettingsModel& model,
                         std::string* out_path) {
    std::string config_path = target_config(opts);
    if (out_path) *out_path = config_path;

    keur::adapters::Fs fs;
    std::string body;
    auto rr = fs.read_file(config_path);
    if (rr.ok) body = rr.content;

    // One backup slot before overwriting (reuse of the manager's .bak contract).
    if (rr.ok && !body.empty()) fs.write_file(config_path + ".bak", body);

    if (auto slash = config_path.find_last_of("/\\"); slash != std::string::npos)
        fs.mkdir_p(config_path.substr(0, slash));

    return fs.write_file(config_path, upsert_difficulty(body, settings_vector(model)));
}

int settings(const SettingsOptions& opts) {
    SettingsModel model = load_settings_model();

    if (opts.print_only) {
        bool color = !opts.no_color && std::getenv("NO_COLOR") == nullptr;
        std::cout << render_settings_screen(model, color);
        return 0;
    }

    bool color = color_enabled(opts);
    platform::RawModeGuard guard;
    if (!guard.active()) {
        std::cerr << "keur settings: no interactive terminal; "
                     "use `keur settings --print` for a non-interactive view.\n";
        return 1;
    }

    bool running = true;
    int exit_code = 0;
    while (running) {
        std::cout << "\x1b""[2J\x1b""[H";
        std::cout << render_settings_screen(model, color) << std::flush;

        platform::KeyEvent ev = platform::read_key();
        switch (ev.key) {
            case platform::Key::Up:    settings_move(model, -1); break;
            case platform::Key::Down:  settings_move(model, +1); break;
            case platform::Key::Right: {
                // On a group header expand; on a slider, nudge up.
                if (!settings_expand(model)) settings_adjust(model, +1);
                break;
            }
            case platform::Key::Left: {
                // On a slider, nudge down; on a group header collapse.
                auto rows = settings_visible_rows(model);
                bool on_slider = model.cursor >= 0 &&
                                 model.cursor < isize(rows) &&
                                 !rows[static_cast<std::size_t>(model.cursor)].is_group();
                if (on_slider) settings_adjust(model, -1);
                else settings_collapse(model);
                break;
            }
            case platform::Key::Enter:
                // Enter toggles expand/collapse on a group (quick disclosure).
                if (!settings_expand(model)) settings_collapse(model);
                break;
            case platform::Key::Char:
                if (ev.ch == 'k') settings_move(model, -1);
                else if (ev.ch == 'j') settings_move(model, +1);
                else if (ev.ch == '+' || ev.ch == '=') settings_adjust(model, +1);
                else if (ev.ch == '-' || ev.ch == '_') settings_adjust(model, -1);
                else if (ev.ch == 's') {
                    std::string cfg;
                    if (save_settings_model(opts, model, &cfg)) {
                        model.dirty = false;
                        std::cout << "\nSaved difficulty to " << cfg << "\n";
                    } else {
                        std::cerr << "\nkeur settings: could not write " << cfg << "\n";
                        exit_code = 1;
                    }
                    running = false;
                } else if (ev.ch == 'q') {
                    running = false;
                }
                break;
            case platform::Key::Escape:
                running = false;
                break;
            default:
                break;
        }
    }
    return exit_code;
}

}  // namespace keur::cmd
