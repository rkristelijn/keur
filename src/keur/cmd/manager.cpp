/**
 * @file cmd/manager.cpp
 * @brief keur manager --status — aggregate config/hooks/scanners/rules into one
 *        read-only board. Orchestrator only: it gathers via the pure config
 *        modules + injected adapters, then renders. @implements ADR-003
 */
#include "cmd/manager.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

#include "cmd/config.h"
#include "cmd/hook.h"
#include "cmd/policy.h"
#include "cmd/rules.h"
#include "common/platform.h"
#include "config/disabled.h"
#include "config/ruleset.h"
#include "config/scopes.h"
#include "manager/catalog.h"
#include "manager/model.h"
#include "manager/view.h"
#include "rules/rule_engine.h"
#include "util/keur_utils.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// manager_parse
// ---------------------------------------------------------------------------

ManagerOptions manager_parse(const std::vector<std::string>& args) {
    ManagerOptions o;
    bool saw_status = false;
    bool saw_positional = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") o.json = true;
        else if (args[i] == "--status") { o.subcommand = "status"; saw_status = true; }
        else if (args[i] == "--no-color") o.no_color = true;
        else if (args[i] == "--config" && i + 1 < args.size()) o.config = args[++i];
        else if (!args[i].empty() && args[i][0] != '-') { o.subcommand = args[i]; saw_positional = true; }
    }
    // Bare `keur manager` (no --status, no --json, no positional subcommand)
    // launches the interactive tree TUI. Any explicit flag/subcommand keeps the
    // read-only status behaviour (and stays scriptable / CI-safe).
    if (!saw_status && !saw_positional && !o.json) o.interactive = true;
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
    out.emplace_back("Config scopes (general → specific):");
    for (const auto& l : board.config_layers)
        out.emplace_back(config_layer_line(l));

    // Hooks
    out.emplace_back("");
    out.emplace_back("Hooks:");
    out.emplace_back("  effective owner : " + config::owner_name(board.hooks.owner) +
                     (board.hooks.effective_path.empty()
                          ? ""
                          : "  (core.hooksPath → " + board.hooks.effective_path + ")"));
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
// Interactive tree TUI (bare `keur manager`)
// ---------------------------------------------------------------------------

namespace {

// Turn the baked catalogue into per-scope RuleItems, marking each disabled when
// its id is in that scope's disabled-set. `governing` is a MORE-SPECIFIC scope's
// disabled-set (e.g. the repo's, when building the global rows): ids the repo
// overrides are marked `inherited` so the global row shows greyed + "(overruled
// by repo)" — the effective decision lives in the more specific scope.
std::vector<manager::RuleItem> rule_items_for_scope(
    const std::set<std::string>& disabled,
    const std::set<std::string>& governing = {}) {
    std::vector<manager::RuleItem> items;
    const auto& cat = manager::catalog();
    items.reserve(cat.size());
    for (const auto& e : cat) {
        bool inherited = governing.contains(e.id);
        manager::RuleItem it;
        it.category = e.category;
        it.subcategory = e.subcategory;
        it.language = e.language;
        it.framework = e.framework;
        it.id = e.id;
        it.detail = e.description;
        it.enabled = !disabled.count(e.id);
        it.deprecated = e.deprecated;
        it.inherited = inherited;
        items.push_back(std::move(it));
    }
    return items;
}

// Resolve the disabled-set for one scope by reading only that scope's
// keur.toml (not the whole stack) — the tree shows/edits each scope's own list.
std::set<std::string> disabled_in_scope(const std::string& path, const adapters::Fs& fs) {
    if (path.empty()) return {};
    auto rr = fs.read_file(path);
    return rr.ok ? config::parse_disabled(rr.content) : std::set<std::string>{};
}

// Build the toggleable scanner list for a scope. The default inventory comes
// from the shipped template; each scope's own keur.toml can override a
// scanner's `enabled`. on_path + install_hint are resolved for the current OS.
// `governing_off` are scanner names disabled by a more specific scope (repo),
// used to mark the global row overruled.
std::vector<manager::ScannerItem> scanner_items_for_scope(
    const std::string& template_body, const std::string& scope_body,
    const adapters::Fs& fs, const std::string& repo_root,
    const std::set<std::string>& governing_off = {}) {
    // Start from the template inventory (names + default enabled). When no
    // template resolves (e.g. KEUR_HOME unset), fall back to the baked default
    // inventory so the Scanners branch is never empty.
    auto declared = config::parse_scanners(template_body);
    if (declared.empty()) declared = config::default_scanners();
    // Apply this scope's enabled overrides.
    auto overrides = config::parse_scanners(scope_body);
    config::ScannerProbe probe;
    probe.tool_on_path = [](const std::string& t) { return keur::platform::tool_on_path(t); };
    probe.exists = [&fs](const std::string& p) { return fs.exists(p); };
    probe.repo_root = repo_root;
    auto resolved = config::resolve_scanners(declared, probe);

    std::vector<manager::ScannerItem> items;
    for (const auto& sc : resolved) {
        bool enabled = sc.enabled;
        for (const auto& ov : overrides) if (ov.name == sc.name) enabled = ov.enabled;
        manager::ScannerItem it;
        it.name = sc.name;
        it.group = sc.group;
        it.enabled = enabled;
        it.on_path = sc.on_path;
        it.install_hint = sc.on_path ? "" : config::install_hint(sc.name);
        it.inherited = governing_off.contains(sc.name);
        items.push_back(std::move(it));
    }
    return items;
}

// Build the read-only Hooks info for a scope: which manager owns hooks here,
// whether keur's global hook is effective or shadowed, and whether keur is
// chained (augmented) into the manager's hook. For the repo scope this is the
// live situation of the current repo; for global it's the machine baseline.
std::vector<manager::HookInfoItem> hook_info_for_scope(
    manager::Scope scope, const config::HookSituation& hs, const std::string& repo_root,
    const adapters::Fs& fs) {
    std::vector<manager::HookInfoItem> out;
    if (scope == manager::Scope::Global) {
        // The machine baseline: is a global core.hooksPath set to keur's dir?
        out.push_back({"global hooksPath",
                       hs.global_hooks_path.empty() ? "(unset)" : hs.global_hooks_path});
        return out;
    }
    // Repo scope: the live situation, including whether a manager shadows keur.
    std::string owner = config::owner_name(hs.owner);
    bool shadowed = !hs.local_hooks_path.empty() && hs.owner != config::HookOwner::Keur;

    // Compute `chained` up front: if keur is shadowed but chained into the
    // manager hook, keur DOES run here, so it is no longer a problem.
    std::string target;
    if (!repo_root.empty()) {
        std::string husky_pc = keur::platform::path_join(
            keur::platform::path_join(repo_root, ".husky"), "pre-commit");
        std::string git_pc = keur::platform::path_join(
            keur::platform::path_join(keur::platform::path_join(repo_root, ".git"), "hooks"),
            "pre-commit");
        target = fs.exists(husky_pc) ? husky_pc : (fs.exists(git_pc) ? git_pc : std::string());
    }
    bool chained = false;
    if (!target.empty()) { auto rr = fs.read_file(target); chained = rr.ok && keur::cmd::is_augmented(rr.content); }

    out.push_back({"effective owner", owner});
    // "keur global" is a warning ONLY when keur does not run here at all
    // (shadowed AND not chained). Shadowed-but-chained is fine — keur runs.
    if (shadowed && chained)
        out.push_back({"keur global", "shadowed by " + owner + ", but keur is chained (runs)", /*warn=*/false});
    else if (shadowed)
        out.push_back({"keur global", "shadowed by " + owner + " (repo-local core.hooksPath)", /*warn=*/true});
    else if (!hs.global_hooks_path.empty())
        out.push_back({"keur global", "effective", false});
    else
        out.push_back({"keur global", "not installed", /*warn=*/true});
    // Detected coexisting tools, labelled manager (owns hooks) vs runner (runs
    // inside a hook — not an owner).
    for (const auto& m : hs.managers)
        if (m.detected) {
            const char* kind = (m.kind == config::HookKind::Runner) ? "runner" : "manager";
            out.push_back({std::string(kind) + " detected", m.name, false});
        }
    // Chained state + the interactive fix/restore action.
    if (!target.empty()) {
        out.push_back({"keur chained", chained ? "yes (keur hook --augment)"
                                                : "no  (run: keur hook --augment)",
                       /*warn=*/(!chained && shadowed), /*action=*/""});
        if (chained)
            out.push_back({"restore " + owner + " (remove keur)", "", false, "unaugment"});
        else if (shadowed)
            out.push_back({"fix this with keur hook --augment", "", false, "augment"});
    }
    return out;
}

// Clear screen + home cursor, then print the frame. Lines are separated by
// CRLF but the LAST line gets NO trailing newline: a full-height frame that
// ends in a newline would push the terminal up by one and scroll the top line
// (the title) off-screen. Mirrors monitor.cpp but fixes that off-by-one.
void draw(const std::vector<std::string>& lines) {
    std::string frame = "\033[2J\033[H";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        frame += lines[i];
        if (i + 1 < lines.size()) frame += "\r\n";
    }
    std::cout << frame << std::flush;
}

// Persist every pending edit through the shared rules_toggle writer. Returns
// the number of edits that FAILED to write (0 = all saved). rules_toggle prints
// its own per-file error; we surface a single summary line in the TUI so a
// failed save is visible (not lost under the alt-screen) and dirty is kept.
int save_pending(const manager::Model& model) {
    // Plan → apply: write each editable scope's COMPLETE desired state in ONE
    // transaction (not one write per toggle). Only scopes that actually have
    // pending edits are touched, so a clean scope's keur.toml is left alone.
    int failed = 0;
    bool global_dirty = false, repo_dirty = false;
    for (const auto& e : manager::pending_edits(model)) {
        if (e.scope == manager::Scope::Global) global_dirty = true;
        else if (e.scope == manager::Scope::Repo) repo_dirty = true;
    }
    if (global_dirty) {
        auto st = manager::desired_state(model, manager::Scope::Global);
        if (apply_scope("global", st.disabled_rules, st.scanner_enabled) != 0) ++failed;
    }
    if (repo_dirty) {
        auto st = manager::desired_state(model, manager::Scope::Repo);
        if (apply_scope("repo", st.disabled_rules, st.scanner_enabled) != 0) ++failed;
    }
    return failed;
}

}  // namespace

int manager_interactive(const ManagerOptions& opts, const ManagerDeps& deps) {
    std::string repo_root = deps.git.run("rev-parse --show-toplevel");
    bool repo_available = !repo_root.empty();

    // Per-scope disabled sets, read from each scope's own keur.toml.
    config::ScopeEnv env = config::capture_scope_env(opts.config, repo_root);
    std::string global_path = config::global_config_path(env);
    std::string repo_path   = config::repo_config_path(env);

    auto global_disabled = disabled_in_scope(global_path, deps.fs);
    auto repo_disabled   = repo_available ? disabled_in_scope(repo_path, deps.fs)
                                          : std::set<std::string>{};
    // Cascade: the MORE SPECIFIC scope wins. A rule set in the repo overrides
    // the global one, so the GLOBAL row for that id is marked overruled (greyed
    // with "(overruled by repo)") — the effective decision for this repo lives
    // in the repo scope. Global's own value no longer applies here.
    auto global_items = rule_items_for_scope(global_disabled, /*governing=*/repo_disabled);
    auto repo_items   = repo_available
                            ? rule_items_for_scope(repo_disabled)
                            : std::vector<manager::RuleItem>{};

    // Defaults branch: read the shipped template for scanner/policy settings,
    // plus the full rule catalogue as the baseline (every rule shown "on").
    manager::DefaultsInfo defaults;
    {
        std::string tmpl_path = resolve_config(opts.config);
        std::string tmpl;
        if (!tmpl_path.empty()) { auto rr = deps.fs.read_file(tmpl_path); if (rr.ok) tmpl = rr.content; }
        for (const auto& sc : config::parse_scanners(tmpl)) {
            std::string v = sc.group;
            if (!sc.default_action.empty()) v += (v.empty() ? "" : " ") + sc.default_action;
            if (!sc.enabled) v += " (disabled)";
            defaults.settings.push_back({"Scanners", sc.name, v});
        }
        // A couple of headline policy values, parsed leniently from the body.
        auto policy_val = [&tmpl](const std::string& key) -> std::string {
            std::size_t p = tmpl.find(key);
            if (p == std::string::npos) return "";
            std::size_t eq = tmpl.find('=', p);
            if (eq == std::string::npos) return "";
            std::size_t nl = tmpl.find('\n', eq);
            std::string v = tmpl.substr(eq + 1, (nl == std::string::npos ? tmpl.size() : nl) - eq - 1);
            std::size_t a = v.find_first_not_of(" \t");
            std::size_t b = v.find_first_of(" \t#", a == std::string::npos ? 0 : a);
            if (a == std::string::npos) return "";
            return v.substr(a, (b == std::string::npos ? v.size() : b) - a);
        };
        for (const char* k : {"block-at", "warn-at"}) {
            std::string v = policy_val(k);
            if (!v.empty()) defaults.settings.push_back({"Policy", k, v});
        }
        // Catalogue baseline: the full set, all enabled.
        defaults.catalog = rule_items_for_scope({});
    }

    // Scanner branches per scope: default inventory from the template, each
    // scope's keur.toml overriding `enabled`. Repo overrides global (cascade).
    std::string tmpl_body;
    { std::string p = resolve_config(opts.config); if (!p.empty()) { auto rr = deps.fs.read_file(p); if (rr.ok) tmpl_body = rr.content; } }
    std::string global_body, repo_body;
    { auto rr = deps.fs.read_file(global_path); if (rr.ok) global_body = rr.content; }
    if (repo_available) { auto rr = deps.fs.read_file(repo_path); if (rr.ok) repo_body = rr.content; }
    // Scanner names the repo turns off, to mark the global row overruled.
    std::set<std::string> repo_scanner_off;
    for (const auto& sc : config::parse_scanners(repo_body))
        if (!sc.enabled) repo_scanner_off.insert(sc.name);
    auto global_scanners = scanner_items_for_scope(tmpl_body, global_body, deps.fs, repo_root, repo_scanner_off);
    auto repo_scanners   = repo_available
                               ? scanner_items_for_scope(tmpl_body, repo_body, deps.fs, repo_root)
                               : std::vector<manager::ScannerItem>{};

    // Hooks info per scope: detect the live hook situation (which manager owns
    // hooks, is keur shadowed, is keur chained) so the tree shows it read-only.
    config::HookProbe hp;
    hp.git_config = [&deps](const std::string& a) { return deps.git.run(a); };
    hp.exists = [&deps](const std::string& p) { return deps.fs.exists(p); };
    hp.repo_root = repo_root;
    config::HookSituation hs = config::detect_hook_situation(hp);
    auto global_hooks = hook_info_for_scope(manager::Scope::Global, hs, repo_root, deps.fs);
    auto repo_hooks   = repo_available
                            ? hook_info_for_scope(manager::Scope::Repo, hs, repo_root, deps.fs)
                            : std::vector<manager::HookInfoItem>{};

    manager::Model model = manager::build_model(defaults, global_items, repo_items,
                                                repo_available, global_path, repo_path,
                                                manager::default_facet_order(),
                                                global_scanners, repo_scanners,
                                                global_hooks, repo_hooks);

    // Colour off when NO_COLOR is set, --no-color given, or output is not a tty.
    bool color = !opts.no_color && std::getenv("NO_COLOR") == nullptr;

    platform::RawModeGuard guard;
    if (!guard.active()) {
        std::cerr << "keur manager: no interactive terminal; "
                     "use `keur manager --status` for a read-only view.\n";
        return 1;
    }

    // Run a hook action (chain/unchain keur into the repo's manager hook) from
    // an ActionNode, then rebuild the model so the Hooks branch refreshes (the
    // ⚠ clears once keur is chained). Preserves the cursor position.
    // Local helper invoked immediately in this scope (not stored), capturing
    // ~10 locals; an explicit list adds noise without removing dangling risk.
    auto run_action = [&](const std::string& action) {  // keur:ignore KEUR-CPP-001 — reviewed: local, not stored
        if (action.empty() || repo_root.empty()) return;
        keur::cmd::HookOptions ho;
        ho.action = keur::cmd::HookAction::Augment;
        ho.remove_augment = (action == "unaugment");
        keur::cmd::hook(ho);                       // writes .husky/.git hook file
        // Re-gather hook info + rebuild, keeping current disabled/scanner state.
        config::HookSituation hs2 = config::detect_hook_situation(hp);
        auto gh = hook_info_for_scope(manager::Scope::Global, hs2, repo_root, deps.fs);
        auto rh = hook_info_for_scope(manager::Scope::Repo, hs2, repo_root, deps.fs);
        auto gd = disabled_in_scope(global_path, deps.fs);
        auto rd = repo_available ? disabled_in_scope(repo_path, deps.fs) : std::set<std::string>{};
        auto g2 = rule_items_for_scope(gd, rd);
        auto r2 = repo_available ? rule_items_for_scope(rd) : std::vector<manager::RuleItem>{};
        int cur = model.cursor;
        model = manager::build_model(defaults, g2, r2, repo_available, global_path, repo_path,
                                     model.order, global_scanners, repo_scanners, gh, rh);
        manager::move_cursor(model, cur);
    };

    bool running = true;
    int exit_code = 0;
    while (running) {
        platform::TermSize ts = platform::terminal_size();
        manager::ViewOpts vo;
        vo.rows  = ts.rows > 0 ? ts.rows : 24;
        vo.cols  = ts.cols > 0 ? ts.cols : 80;
        vo.color = color;
#ifdef KEUR_VERSION
        vo.version = KEUR_VERSION;
#endif
        draw(manager::render(model, vo));

        platform::KeyEvent ev = platform::read_key();

        // Search input mode: keystrokes build the live `/` filter until Enter
        // (keep the filter) or Esc (clear it). Everything else is captured here
        // so typing a query never triggers navigation/toggles.
        if (model.search_active) {
            switch (ev.key) {
                case platform::Key::Enter:     manager::search_commit(model); break;
                case platform::Key::Escape:    manager::search_clear(model); break;
                case platform::Key::Backspace: manager::search_backspace(model); break;
                case platform::Key::Space:     manager::search_input(model, ' '); break;
                case platform::Key::Char:      manager::search_input(model, ev.ch); break;
                default: break;
            }
            continue;
        }

        switch (ev.key) {
            case platform::Key::Up:    manager::move_cursor(model, -1); break;
            case platform::Key::Down:  manager::move_cursor(model, +1); break;
            case platform::Key::Left:  manager::collapse_focused(model); break;
            case platform::Key::Right: manager::expand_focused(model); break;
            case platform::Key::Enter: {
                std::string act = manager::focused_action(model);
                if (!act.empty()) run_action(act);
                else manager::toggle_expand_focused(model);
                break;
            }
            case platform::Key::Space: {
                std::string act = manager::focused_action(model);
                if (!act.empty()) run_action(act);
                else manager::toggle_focused_group(model);
                break;
            }
            case platform::Key::Escape:
                // Clear a committed `/` filter (when not in input mode).
                if (!model.search_query.empty()) manager::search_clear(model);
                break;
            case platform::Key::Char:
                switch (ev.ch) {
                    case '/': manager::search_begin(model); break;
                    case 'k': manager::move_cursor(model, -1); break;
                    case 'j': manager::move_cursor(model, +1); break;
                    case 'h': manager::collapse_focused(model); break;
                    case 'l': manager::expand_focused(model); break;
                    case 'g': manager::cursor_top(model); break;
                    case 'G': manager::cursor_bottom(model); break;
                    case 'o': {
                        // Reorder the grouping (category-first ↔ language-first).
                        // Rebuild from the persisted disabled-sets under the new
                        // facet order. Guard on unsaved edits so a regroup never
                        // silently drops them.
                        if (model.dirty) {
                            draw({"Save (s) or discard before reordering — unsaved changes.",
                                  "Press any key to continue."});
                            platform::read_key();
                            break;
                        }
                        auto gd = disabled_in_scope(global_path, deps.fs);
                        auto rd = repo_available ? disabled_in_scope(repo_path, deps.fs)
                                                 : std::set<std::string>{};
                        auto g2 = rule_items_for_scope(gd, rd);
                        auto r2 = repo_available ? rule_items_for_scope(rd)
                                                 : std::vector<manager::RuleItem>{};
                        auto next = manager::next_facet_order(model.order);
                        model = manager::build_model(defaults, g2, r2, repo_available,
                                                     global_path, repo_path, next,
                                                     global_scanners, repo_scanners,
                                                     global_hooks, repo_hooks);
                        break;
                    }
                    case 's': {
                        // Save: persist every pending edit through the SAME
                        // writer the CLI uses, then rebaseline IN-PLACE so the
                        // tree's expand-state and cursor are preserved. On a
                        // write failure, keep dirty and show one message.
                        int failed = save_pending(model);
                        if (failed == 0) {
                            manager::rebaseline(model);
                        } else {
                            draw({"Save failed: could not write " + std::to_string(failed) +
                                  " change(s). Check permissions on the keur.toml path.",
                                  "Press any key to continue."});
                            platform::read_key();
                        }
                        break;
                    }
                    case 'q': {
                        if (model.dirty) {
                            // Prompt: read one key for y/n/c.
                            draw({"Unsaved changes. Save before quitting? [y]es  [n]o  [c]ancel"});
                            platform::KeyEvent a = platform::read_key();
                            char c = (a.key == platform::Key::Char) ? a.ch : '\0';
                            if (c == 'y' || c == 'Y') {
                                int failed = save_pending(model);
                                if (failed == 0) {
                                    manager::rebaseline(model);
                                    running = false;
                                } else {
                                    // Don't lose edits on a failed save: show
                                    // the error and stay in the editor.
                                    draw({"Save failed: could not write " + std::to_string(failed) +
                                          " change(s). Staying open so nothing is lost.",
                                          "Press any key to continue."});
                                    platform::read_key();
                                }
                            } else if (c == 'n' || c == 'N') {
                                running = false;  // discard
                            }
                            // 'c' or anything else: cancel, stay in the loop.
                        } else {
                            running = false;
                        }
                        break;
                    }
                    default: break;
                }
                break;
            default: break;
        }
    }

    // Restore a clean screen on exit.
    std::cout << "\033[2J\033[H" << std::flush;
    return exit_code;
}

// ---------------------------------------------------------------------------
// manager — orchestrator (read-only status board + interactive dispatch)
// ---------------------------------------------------------------------------

int manager(const ManagerOptions& opts, const ManagerDeps& deps) {
    if (opts.interactive) return manager_interactive(opts, deps);

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
        env, [&deps](const std::string& p) { return deps.fs.exists(p); });

    // Hook situation (+ coexisting managers).
    config::HookProbe hp;
    hp.git_config = [&deps](const std::string& a) { return deps.git.run(a); };
    hp.exists = [&deps](const std::string& p) { return deps.fs.exists(p); };
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
    sp.exists = [&deps](const std::string& p) { return deps.fs.exists(p); };
    sp.repo_root = repo_root;
    board.scanners = config::resolve_scanners(declared, sp);

    // Rules by category.
    board.rule_categories = count_rules_by_category(board.rule_total);

    if (opts.json) std::cout << manager_render_json(board) << "\n";
    else for (const auto& line : manager_render_text(board)) std::cout << line << "\n";
    return 0;
}

}  // namespace keur::cmd
