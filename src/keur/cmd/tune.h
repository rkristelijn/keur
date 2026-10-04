/**
 * @file cmd/tune.h
 * @brief keur tune — one screen, three lenses, switched with ←/→.
 *
 *   level  ←→  settings  ←→  manager
 *   COARSE      MEDIOR        EXPERT
 *
 * The three views are different lenses on the ONE canonical difficulty state
 * (keur.toml [difficulty] vector + [difficulty.rules]); ←/→ only changes which
 * lens is active, up/down/+/- act within the active lens. This router reuses
 * the existing per-view renderers rather than reimplementing them, so the
 * stable `keur level` / `keur manager` loops are not rewritten.
 *
 * This first cut ships a working level↔settings toggle with a DUMMY medior
 * screen (the real sliders land next); the expert lens hands off to the
 * existing `keur manager`.
 *
 * The lens selection + the header are PURE (unit-testable); the I/O edge
 * (raw mode, keys) lives in tune(), mirroring cmd/level.cpp.
 *
 * @see docs/design/difficulty-chooser-ui.md
 * @id KEUR-TUNE
 */
#pragma once
#include <string>
#include <vector>

namespace keur::cmd {

/// Which lens is active. Order is the ←/→ axis: coarse → medior → expert.
/// @id KEUR-TUNE.view
enum class TuneView { Level, Settings, Manager };

/// The next/previous lens along the ←/→ axis, clamped at the ends (no wrap, so
/// the ends feel like walls — you cannot fall off coarse or expert). Pure.
/// @id KEUR-TUNE.step
[[nodiscard]] TuneView tune_next(TuneView v);
[[nodiscard]] TuneView tune_prev(TuneView v);

/// The lens tab bar, e.g. "[ level ] ·  settings  ·  manager", with the active
/// lens marked. Pure, deterministic given the view + color flag.
/// @id KEUR-TUNE.header
[[nodiscard]] std::string render_tune_header(TuneView active, bool color);

/// The DUMMY medior screen — a placeholder until the real sliders are wired in.
/// Pure. @id KEUR-TUNE.medior-dummy
[[nodiscard]] std::string render_medior_dummy(bool color);

/// Parsed `keur tune` invocation.
struct TuneOptions {
    std::string config;              ///< --config path (empty = auto-resolve)
    bool        no_color   = false;  ///< --no-color
    bool        print_only = false;  ///< --print: render once, no tty
    TuneView    start      = TuneView::Level;  ///< --view level|settings|manager
};

/// Parse argv into TuneOptions. Pure, never fails. @id KEUR-TUNE.parse
[[nodiscard]] TuneOptions tune_parse(const std::vector<std::string>& args);

/// Run `keur tune`. Interactive: ←/→ switch lens, the active lens handles the
/// rest; `q`/Esc quits. --print renders the start lens once. Returns 0 on
/// success. @id KEUR-TUNE
int tune(const TuneOptions& opts);

}  // namespace keur::cmd
