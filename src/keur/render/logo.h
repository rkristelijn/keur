/**
 * @file render/logo.h
 * @brief The KEUR DOOM-style logo as responsive coloured braille + the
 *        terminal-width -> size bucket mapping. The braille strings are baked
 *        offline by scripts/gen-logo.sh into logo_generated.cpp (ADR-002: no
 *        runtime image library in the gate). @see docs/design/doom-level-select.md
 * @id KEUR-LOGO
 */
#pragma once

namespace keur::render {

/// Terminal-width size buckets for the logo (and the level-select layout).
enum class Bucket { Sm, Md, Lg, Xl };

/// Pick the size bucket for a terminal width in columns. Pure — table-testable.
/// Thresholds: <=60 Sm, 61..100 Md, 101..140 Lg, >140 Xl.
/// @id KEUR-LOGO.bucket
[[nodiscard]] Bucket bucket_for_width(int cols);

/// The rendered height of a bucket's logo, in terminal rows. Pure.
/// @id KEUR-LOGO.height
[[nodiscard]] int logo_height(Bucket b);

/// The largest bucket that fits BOTH the terminal width and height. Prevents a
/// tall logo from scrolling its top off-screen (which would truncate "KEUR" to
/// its bottom half). `rows_available` is the terminal height minus the space the
/// level-select UI needs below the logo (title + 5 rows + blurb). Falls back to
/// Sm when nothing fits, so something legible always shows. Pure — table-tested.
/// @id KEUR-LOGO.fit
[[nodiscard]] Bucket bucket_for_size(int cols, int rows_available);

/// The baked logo for a bucket. `color` picks the ANSI-256 coloured variant
/// (DOOM orange/blue gradient) vs plain braille. Never null. Generated.
[[nodiscard]] const char* keur_logo(Bucket b, bool color);

}  // namespace keur::render
