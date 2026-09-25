/**
 * @file cmd/level.cpp
 * @brief keur level — DOOM "Choose your Difficulty" screen (see level.h).
 *        Pure rendering + parsing here; I/O (terminal size, key input, config
 *        write) is routed through common/platform and the Fs adapter (ADR-003).
 * @implements KEUR-LEVEL
 * @implements ADR-026
 */
#include "cmd/level.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "cmd/policy.h"
#include "common/platform.h"

namespace keur::cmd {

namespace {

// DOOM-menu red (ANSI-256 196) for the title + level list; a dimmer red (124)
// for the unselected rows so the selection stands out. @implements KEUR-LEVEL
constexpr const char* k_red        = "\033[38;5;196m";
constexpr const char* k_red_dim    = "\033[38;5;124m";
constexpr const char* k_reset      = "\033[0m";

std::string paint(const std::string& s, const char* code, bool color) {
    if (!color) return s;
    return std::string(code) + s + k_reset;
}

}  // namespace

// ---------------------------------------------------------------------------
// level_table — the five fixed levels (index 0..4 == CMMI 1..5).
// @implements KEUR-LEVEL.table
// ---------------------------------------------------------------------------
const std::vector<LevelInfo>& level_table() {
    static const std::vector<LevelInfo> kLevels = {
        {1, "I'm Too Young to KEUR", "Initial",
         "Essential checks only: secrets, committed keys, build-breakers."},
        {2, "Hey, Not Too Sure", "Managed",
         "A practical baseline of checks for repeatable development."},
        {3, "KEUR Me Plenty", "Defined",
         "The standard KEUR experience: maintainability + correctness."},
        {4, "Ultra Compliance", "Quantitatively Managed",
         "Stricter and measured: coverage, traceability, style consistency."},
        {5, "H4x0r c0d3fl3x", "Optimizing",
         "Maximum guardrails: advisory nudges and opinionated idiom rules."},
    };
    return kLevels;
}

// Keep the compile-time count honest against the table (checked in a unit test:
// level_table().size() == k_level_count). A static_assert cannot see the
// runtime vector size, so the invariant is proven by @proves KEUR-LEVEL.table.

// ---------------------------------------------------------------------------
// bucket_from_name — pure name -> Bucket. @implements KEUR-LEVEL.bucket_name
// ---------------------------------------------------------------------------
bool bucket_from_name(const std::string& name, render::Bucket& out) {
    if (name == "sm") { out = render::Bucket::Sm; return true; }
    if (name == "md") { out = render::Bucket::Md; return true; }
    if (name == "lg") { out = render::Bucket::Lg; return true; }
    if (name == "xl") { out = render::Bucket::Xl; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// level_parse — pure argv parsing. @implements KEUR-LEVEL.parse
// ---------------------------------------------------------------------------
LevelOptions level_parse(const std::vector<std::string>& args) {
    LevelOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--no-color") {
            o.no_color = true;
        } else if (a == "--config" && i + 1 < args.size()) {
            o.config = args[++i];
        } else if (a == "--print" && i + 1 < args.size()) {
            try {
                o.print_level = std::stoi(args[++i]);
                o.print_only  = true;
            } catch (const std::exception&) { /* leave unset; validated later */ }
        } else if (a == "--bucket" && i + 1 < args.size()) {
            render::Bucket b;
            if (bucket_from_name(args[++i], b)) {
                o.bucket     = b;
                o.bucket_set = true;
                o.print_only = true;
            }
        }
    }
    return o;
}

// The chooser UI below the logo needs ~12 rows (blank, title, blank, 5 levels,
// blank, up to 2 blurb lines). The smallest logo (Sm) is 13 rows. Show the logo
// only when both fit; otherwise omit it so nothing clips. @implements KEUR-LEVEL.fits-logo
bool fits_logo(int rows) {
    constexpr int kUiRows = 12;
    return rows >= render::logo_height(render::Bucket::Sm) + kUiRows;  // >= 25
}

// ---------------------------------------------------------------------------
// render_level_screen — pure. logo (braille) + title + 5 text rows + blurb.
// @implements KEUR-LEVEL.render
// ---------------------------------------------------------------------------
std::string render_level_screen(render::Bucket bucket, bool color, int selected,
                                bool show_logo) {
    const auto& levels = level_table();
    if (selected < 1) selected = 1;
    if (selected > k_level_count) selected = k_level_count;

    std::ostringstream os;
    // The logo is omitted on short terminals (see fits_logo): a clipped logo
    // scrolls its top off-screen and reads as "neuk" instead of "KEUR", so we'd
    // rather show none than half. The chooser below is always shown.
    if (show_logo) os << render::keur_logo(bucket, color) << "\n";
    os << paint("Choose your Difficulty:", k_red, color) << "\n\n";

    for (const auto& lv : levels) {
        bool is_sel = (lv.cmmi == selected);
        std::string marker = is_sel ? "> " : "  ";
        std::string row = marker + lv.name;
        os << paint(row, is_sel ? k_red : k_red_dim, color) << "\n";
    }
    os << "\n";

    const LevelInfo& sel = levels[static_cast<std::size_t>(selected - 1)];
    std::ostringstream head;
    head << "Level " << sel.cmmi << " — " << sel.cmmi_name << ".";
    os << paint(head.str(), k_red, color) << "\n";
    os << sel.description << "\n";
    return os.str();
}

// ---------------------------------------------------------------------------
// upsert_policy_level — pure TOML round-trip. @implements KEUR-LEVEL.persist
// Mirrors config/disabled.cpp's line-oriented upsert: preserve unrelated lines,
// replace the single `level = N` line under [policy], or add section/key.
// ---------------------------------------------------------------------------
std::string upsert_policy_level(const std::string& toml_body, int level) {
    const std::string level_line = "level = " + std::to_string(level);

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

    std::vector<std::string> out;
    bool in_policy = false, wrote = false, saw_policy = false;
    for (const auto& raw : lines) {
        std::string t = trim(raw);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            if (in_policy && !wrote) { out.push_back(level_line); wrote = true; }
            in_policy = (t == "[policy]");
            if (in_policy) saw_policy = true;
            out.push_back(raw);
            continue;
        }
        if (in_policy && t.rfind("level", 0) == 0 && t.find('=') != std::string::npos) {
            out.push_back(level_line);
            wrote = true;
            continue;
        }
        out.push_back(raw);
    }
    if (in_policy && !wrote) { out.push_back(level_line); wrote = true; }

    std::string result;
    for (const auto& l : out) { result += l; result += "\n"; }

    if (!saw_policy) {
        if (!result.empty() && result.back() != '\n') result += "\n";
        result += "\n[policy]\n" + level_line + "\n";
    }
    return result;
}

// ---------------------------------------------------------------------------
// Helpers for the I/O edge (kept tiny; the decisions above are pure).
// ---------------------------------------------------------------------------
namespace {

bool color_enabled(const LevelOptions& opts) {
    // Colour on only when not suppressed and stdout is a real terminal.
    if (opts.no_color) return false;
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return platform::terminal_size().cols > 0;  // 0 cols == no tty
}

render::Bucket detect_bucket() {
    // Rows the level-select UI needs BELOW the logo: a blank line, the title, a
    // blank, the 5 level rows, a blank, and up to 2 blurb lines ≈ 11. Reserve
    // that (plus 1 margin) so the logo is sized to fit what's left — otherwise a
    // tall logo scrolls its top off-screen and "KEUR" truncates to its bottom.
    constexpr int kUiRows = 12;

    // Prefer real terminal dimensions; COLUMNS/LINES env override (both must be
    // usable to trust the pair). Fall back to a sane 80x24 when piped.
    int cols = 0, rows = 0;
    if (const char* c = std::getenv("COLUMNS"); c && *c) {
        try { cols = std::stoi(c); } catch (const std::exception&) { cols = 0; }
    }
    if (const char* r = std::getenv("LINES"); r && *r) {
        try { rows = std::stoi(r); } catch (const std::exception&) { rows = 0; }
    }
    if (cols <= 0 || rows <= 0) {
        auto ts = platform::terminal_size();
        if (cols <= 0) cols = ts.cols;
        if (rows <= 0) rows = ts.rows;
    }
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;
    return render::bucket_for_size(cols, rows - kUiRows);
}

// The terminal height in rows (LINES env, else ioctl, else 24 when piped).
// Used to decide whether the logo fits at all (fits_logo). @id KEUR-LEVEL.rows
int detect_rows() {
    if (const char* r = std::getenv("LINES"); r && *r) {
        try { int v = std::stoi(r); if (v > 0) return v; } catch (const std::exception&) {}
    }
    int rows = platform::terminal_size().rows;
    return rows > 0 ? rows : 24;
}

// Persist the chosen level to keur.toml via the Fs adapter. Returns true on
// success. Reads the existing body (empty when the file is new), upserts, writes.
bool persist_level(const std::string& config_path, int level, keur::adapters::Fs& fs) {
    std::string body;
    auto rr = fs.read_file(config_path);
    if (rr.ok) body = rr.content;
    return fs.write_file(config_path, upsert_policy_level(body, level));
}

}  // namespace

// ---------------------------------------------------------------------------
// level — the subcommand entry point.
// ---------------------------------------------------------------------------
int level(const LevelOptions& opts) {
    const int max_level = k_level_count;

    // Non-interactive: --print <N> and/or --bucket. Deterministic, no keyboard.
    if (opts.print_only) {
        int sel = opts.print_level >= 1 ? opts.print_level : 3;  // default L3
        if (sel > max_level) sel = max_level;
        render::Bucket b = opts.bucket_set ? opts.bucket : detect_bucket();
        bool color = !opts.no_color && std::getenv("NO_COLOR") == nullptr;
        // Explicit --bucket forces the logo (deterministic for tests/pipes);
        // otherwise show it only when the terminal is tall enough.
        bool show_logo = opts.bucket_set || fits_logo(detect_rows());
        std::cout << render_level_screen(b, color, sel, show_logo);
        return 0;
    }

    // Interactive: draw the screen, arrow keys move the selection, Enter saves.
    bool color = color_enabled(opts);
    render::Bucket bucket = detect_bucket();
    bool show_logo = fits_logo(detect_rows());

    platform::RawModeGuard guard;
    if (!guard.active()) {
        std::cerr << "keur level: no interactive terminal; "
                     "use `keur level --print <1-5>` for a non-interactive view.\n";
        return 1;
    }

    int selected = 3;  // default to the standard KEUR experience (L3)
    bool running = true;
    int exit_code = 0;
    while (running) {
        std::cout << "\033[2J\033[H";  // clear + home
        std::cout << render_level_screen(bucket, color, selected, show_logo) << std::flush;

        platform::KeyEvent ev = platform::read_key();
        switch (ev.key) {
            case platform::Key::Up:   if (selected > 1)         --selected; break;
            case platform::Key::Down: if (selected < max_level) ++selected; break;
            case platform::Key::Enter: {
                auto cfg = resolve_config(opts.config);
                keur::adapters::Fs fs;
                if (persist_level(cfg, selected, fs)) {
                    std::cout << "\nSaved level " << selected << " to " << cfg << "\n";
                } else {
                    std::cerr << "\nkeur level: could not write " << cfg << "\n";
                    exit_code = 1;
                }
                running = false;
                break;
            }
            case platform::Key::Escape: running = false; break;
            case platform::Key::Char:
                if (ev.ch == 'k' && selected > 1)         --selected;
                else if (ev.ch == 'j' && selected < max_level) ++selected;
                else if (ev.ch == 'q') running = false;
                break;
            default: break;
        }
    }
    return exit_code;
}

}  // namespace keur::cmd
