/**
 * @file cmd/tune.cpp
 * @brief keur tune — the ←/→ lens router over level/settings/manager (tune.h).
 *        Pure lens logic + header + dummy medior here; the I/O edge (raw mode,
 *        keys) mirrors cmd/level.cpp. The expert lens hands off to `keur
 *        manager` so its loop is reused, not rewritten.
 * @id KEUR-TUNE
 */
#include "cmd/tune.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "cmd/level.h"
#include "cmd/manager.h"
#include "cmd/policy.h"
#include "cmd/settings.h"
#include "common/platform.h"
#include "config/scopes.h"
#include "adapters/fs.h"
#include "adapters/git.h"

namespace keur::cmd {

namespace {

constexpr const char* k_red     = "\x1b""[38;5;196m";
constexpr const char* k_red_dim = "\x1b""[38;5;124m";
constexpr const char* k_dim     = "\x1b""[2m";
constexpr const char* k_reset   = "\x1b""[0m";

std::string paint(const std::string& s, const char* code, bool color) {
    if (!color) return s;
    return std::string(code) + s + k_reset;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lens stepping — clamped (no wrap). @id KEUR-TUNE.step
// ---------------------------------------------------------------------------
TuneView tune_next(TuneView v) {
    switch (v) {
        case TuneView::Level:    return TuneView::Settings;
        case TuneView::Settings: return TuneView::Manager;
        case TuneView::Manager:  return TuneView::Manager;  // wall
    }
    return v;
}

TuneView tune_prev(TuneView v) {
    switch (v) {
        case TuneView::Level:    return TuneView::Level;    // wall
        case TuneView::Settings: return TuneView::Level;
        case TuneView::Manager:  return TuneView::Settings;
    }
    return v;
}

// ---------------------------------------------------------------------------
// Header tab bar. @id KEUR-TUNE.header
// ---------------------------------------------------------------------------
std::string render_tune_header(TuneView active, bool color) {
    struct Tab { TuneView v; const char* label; };
    static const Tab tabs[] = {
        {TuneView::Level,    "level"},
        {TuneView::Settings, "settings"},
        {TuneView::Manager,  "manager"},
    };
    std::ostringstream os;
    bool first = true;
    for (const auto& t : tabs) {
        if (!first) os << paint("  \xC2\xB7  ", k_dim, color);  // " · "
        first = false;
        if (t.v == active)
            os << paint(std::string("[ ") + t.label + " ]", k_red, color);
        else
            os << paint(t.label, k_red_dim, color);
    }
    os << "\n";
    os << paint("  Tab/\xE2\x87\xA7Tab switch lens  "   // Tab/⇧Tab
                "(coarse \xC2\xB7 medior \xC2\xB7 expert)  \xC2\xB7  q quit",
                k_dim, color);
    os << "\n";
    return os.str();
}

// ---------------------------------------------------------------------------
// tune_parse. @id KEUR-TUNE.parse
// ---------------------------------------------------------------------------
TuneOptions tune_parse(const std::vector<std::string>& args) {
    TuneOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--no-color") o.no_color = true;
        else if (a == "--print") o.print_only = true;
        else if (a == "--config" && i + 1 < args.size()) o.config = args[++i];
        else if (a == "--view" && i + 1 < args.size()) {
            const std::string& v = args[++i];
            if (v == "level") o.start = TuneView::Level;
            else if (v == "settings") o.start = TuneView::Settings;
            else if (v == "manager") o.start = TuneView::Manager;
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Rendering one lens frame. Level = the real DOOM chooser; Settings = the real
// slider model; Manager = a hand-off prompt (the tree opens via keur manager).
// ---------------------------------------------------------------------------
namespace {

std::string render_lens(TuneView v, bool color, int level_sel,
                        const SettingsModel& smodel) {
    std::ostringstream os;
    os << render_tune_header(v, color) << "\n";
    switch (v) {
        case TuneView::Level:
            os << render_level_screen(render::Bucket::Sm, color, level_sel,
                                      /*show_logo=*/false);
            os << "\n" << paint("  \xE2\x86\x91\xE2\x86\x93/\xE2\x86\x90\xE2\x86\x92 pick level \xC2\xB7 Enter save \xC2\xB7 Tab \xE2\x86\x92 settings",
                                k_dim, color) << "\n";
            break;
        case TuneView::Settings:
            os << render_settings_screen(smodel, color, /*show_hint=*/false);
            os << "\n" << paint("  \xE2\x86\x91\xE2\x86\x93 move \xC2\xB7 \xE2\x86\x90\xE2\x86\x92 adjust/expand \xC2\xB7 "
                                "-/+ adjust \xC2\xB7 s save \xC2\xB7 Tab \xE2\x86\x92 manager",
                                k_dim, color) << "\n";
            break;
        case TuneView::Manager:
            os << paint("  manager (expert) — per-rule on/off", k_red, color) << "\n\n";
            os << "  Press Enter to open the full rule tree (keur manager).\n";
            os << paint("  (\xE2\x86\x90 back to settings)", k_dim, color) << "\n";
            break;
    }
    return os.str();
}

// Save the coarse level choice to keur.toml via the same [policy] level upsert
// the level writer uses (one persistence path, no drift).
void save_level(const TuneOptions& opts, int level, std::string* out_msg) {
    adapters::Fs fs{};
    std::string cfg = resolve_config(opts.config);
    std::string body;
    auto rr = fs.read_file(cfg);
    if (rr.ok) body = rr.content;
    if (auto slash = cfg.find_last_of("/\\"); slash != std::string::npos)
        fs.mkdir_p(cfg.substr(0, slash));
    bool ok = fs.write_file(cfg, upsert_policy_level(body, level));
    if (out_msg) *out_msg = ok ? ("saved level " + std::to_string(level) + " to " + cfg)
                               : ("could not write " + cfg);
}

}  // namespace  (render helpers above are file-local)

// --- Per-lens key handlers (public, unit-testable; see tune.h) --------------
// Each handles ONLY its lens's within-view keys (navigation across lenses is
// done by tune() before these run). Keeping them separate keeps the loop a
// thin skeleton instead of one long branching switch.

// Is the settings cursor on a slider row (vs a group header)?
bool settings_on_slider(const SettingsModel& m) {
    auto rows = settings_visible_rows(m);
    const std::size_t cur = m.cursor < 0 ? rows.size() : static_cast<std::size_t>(m.cursor);
    return cur < rows.size() && !rows[cur].is_group();
}

// The coarse chooser is 1-D: up/left step down a level, down/right step up.
void handle_level_key(const platform::KeyEvent& ev, const TuneOptions& opts,
                      int& level_sel, std::string& status) {
    const bool dec = ev.key == platform::Key::Up || ev.key == platform::Key::Left ||
                     (ev.key == platform::Key::Char && (ev.ch == 'k' || ev.ch == 'h'));
    const bool inc = ev.key == platform::Key::Down || ev.key == platform::Key::Right ||
                     (ev.key == platform::Key::Char && (ev.ch == 'j' || ev.ch == 'l'));
    if (dec && level_sel > 1) --level_sel;
    else if (inc && level_sel < 5) ++level_sel;
    else if (ev.key == platform::Key::Enter) save_level(opts, level_sel, &status);
}

// Sliders: delegates the edit logic to the shared settings_apply_key (one
// source of truth with standalone `keur settings`); only the save side differs
// (the router reports via a status line instead of printing + exiting).
void handle_settings_key(const platform::KeyEvent& ev, const TuneOptions& opts,
                         SettingsModel& smodel, std::string& status) {
    if (settings_apply_key(smodel, ev) != SettingsKeyResult::SaveRequested) return;
    SettingsOptions so;
    so.config   = opts.config;
    so.no_color = opts.no_color;
    std::string cfg;
    bool ok = save_settings_model(so, smodel, &cfg);
    smodel.dirty = !ok;
    status = ok ? ("saved difficulty to " + cfg) : ("could not write " + cfg);
}

int tune(const TuneOptions& opts) {
    bool color_print = !opts.no_color && std::getenv("NO_COLOR") == nullptr;

    // The live state behind the lenses: the real slider model + a coarse level.
    SettingsModel smodel = load_settings_model();
    // Seed the coarse level from the resolved vector's default (its overall).
    int level_sel = smodel.groups.empty() ? 3 : [&]{
        int sum = 0, n = 0;
        for (const auto& g : smodel.groups)
            for (const auto& s : g.sliders) if (s.level > 0) { sum += s.level; ++n; }
        int v = (n == 0) ? 3 : (sum + n / 2) / n;  // rounded average
        if (v < 1) v = 1; if (v > 5) v = 5;
        return v;
    }();

    if (opts.print_only) {
        std::cout << render_lens(opts.start, color_print, level_sel, smodel);
        return 0;
    }

    bool color = color_print && platform::terminal_size().cols > 0;
    platform::RawModeGuard guard;
    if (!guard.active()) {
        std::cerr << "keur tune: no interactive terminal; "
                     "use `keur tune --print [--view level|settings|manager]`.\n";
        return 1;
    }

    TuneView view = opts.start;
    bool running = true;
    bool open_manager = false;
    std::string status;  // last action feedback (save result)

    while (running) {
        std::cout << "\x1b""[2J\x1b""[H";
        std::cout << render_lens(view, color, level_sel, smodel);
        if (!status.empty())
            std::cout << "\n" << paint("  " + status, k_dim, color);
        std::cout << std::flush;

        platform::KeyEvent ev = platform::read_key();
        // Global keys: Tab/Shift-Tab switch lens; q/Esc quit. Arrows stay with
        // the active lens (handled below).
        if (ev.key == platform::Key::Tab)     { view = tune_next(view); status.clear(); continue; }
        if (ev.key == platform::Key::BackTab) { view = tune_prev(view); status.clear(); continue; }
        if (ev.key == platform::Key::Escape)  { running = false; continue; }
        if (ev.key == platform::Key::Char && ev.ch == 'q') { running = false; continue; }

        // Delegate the rest to the active lens's handler.
        switch (view) {
            case TuneView::Level:    handle_level_key(ev, opts, level_sel, status); break;
            case TuneView::Settings: handle_settings_key(ev, opts, smodel, status); break;
            case TuneView::Manager:  if (ev.key == platform::Key::Enter) { open_manager = true; running = false; } break;
        }
    }

    // Hand off to the real manager loop. The manager installs its own
    // RawModeGuard, so release ours first by moving it into a scope that
    // destructs here (RawModeGuard has no manual release method).
    if (open_manager) {
        { platform::RawModeGuard released = std::move(guard); (void)released; }
        ManagerOptions mopts;
        mopts.interactive = true;
        mopts.config      = opts.config;
        mopts.no_color    = opts.no_color;
        return manager_interactive(mopts);
    }

    return 0;
}

}  // namespace keur::cmd
