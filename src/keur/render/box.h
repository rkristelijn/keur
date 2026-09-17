/**
 * @file render/box.h
 * @brief Display-width-aware box drawing + tiny chart primitives.
 *
 * Box-drawing character sets (Single/Rounded/Double) are taken from tuigram
 * (https://github.com/rkristelijn/tuigram) src/core/canvas.cpp, MIT — Copyright
 * (c) 2026 Remi Kristelijn (see LICENSE-tuigram). The box/chart helpers here are
 * keur's own, built on the vendored display_width primitives so every border
 * lines up regardless of unicode content.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace keur::render {

/// Box border style. Char order: top-left, top-right, bottom-left,
/// bottom-right, horizontal, vertical. (From tuigram canvas.cpp kSingle etc.)
enum class BoxStyle { Single, Rounded, Double };

struct BoxChars {
  const char* tl;
  const char* tr;
  const char* bl;
  const char* br;
  const char* h;
  const char* v;
};

[[nodiscard]] BoxChars box_chars(BoxStyle style);

/// Render a titled box around the given content lines, sized to `inner_width`
/// display columns. Content lines are padded/truncated to fit. Returns the box
/// as lines (no trailing newline per line).
[[nodiscard]] std::vector<std::string> box(const std::string& title,
                                           const std::vector<std::string>& lines,
                                           int inner_width,
                                           BoxStyle style = BoxStyle::Rounded);

/// Horizontal bar: `frac` (0..1) rendered as `fill` blocks over `width` cols.
[[nodiscard]] std::string bar(double frac, int width, char fill_ascii = '#');

/// Unicode block bar using the eighths ramp for a smooth fill.
[[nodiscard]] std::string block_bar(double frac, int width);

/// Sparkline of values, mapped onto the ▁▂▃▄▅▆▇█ ramp (min..max autoscaled).
[[nodiscard]] std::string sparkline(const std::vector<double>& values);

/// Join two blocks of lines side by side with `gap` spaces between columns.
/// Shorter block is padded with blank lines; each left line padded to its
/// widest display width so the right column aligns.
[[nodiscard]] std::vector<std::string> beside(const std::vector<std::string>& left,
                                              const std::vector<std::string>& right,
                                              int gap = 2);

}  // namespace keur::render
