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

/// The baked logo for a bucket. `color` picks the ANSI-256 coloured variant
/// (DOOM orange/blue gradient) vs plain braille. Never null. Generated.
[[nodiscard]] const char* keur_logo(Bucket b, bool color);

}  // namespace keur::render
