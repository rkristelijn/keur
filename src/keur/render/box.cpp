/**
 * @file render/box.cpp
 * @brief Display-width-aware box drawing + chart primitives — implementation.
 *
 * Box-drawing char sets from tuigram (MIT, (c) 2026 Remi Kristelijn); see box.h.
 */
#include "render/box.h"

#include <algorithm>
#include <array>

#include "render/display_width.h"

namespace keur::render {

BoxChars box_chars(BoxStyle style) {
  switch (style) {
    case BoxStyle::Double:  return {"╔", "╗", "╚", "╝", "═", "║"};
    case BoxStyle::Rounded: return {"╭", "╮", "╰", "╯", "─", "│"};
    case BoxStyle::Single:
    default:                return {"┌", "┐", "└", "┘", "─", "│"};
  }
}

namespace {
/// Repeat a UTF-8 unit string n times.
std::string repeat(const char* unit, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += unit;
  return out;
}
}  // namespace

std::vector<std::string> box(const std::string& title,
                             const std::vector<std::string>& lines,
                             int inner_width, BoxStyle style) {
  const BoxChars bc = box_chars(style);
  if (inner_width < 1) inner_width = 1;
  std::vector<std::string> out;

  // Top border with embedded title: ╭─ title ─────╮
  std::string top = std::string(bc.tl) + bc.h;
  std::string label = title.empty() ? "" : (" " + title + " ");
  int label_w = display_width(label);
  if (label_w > inner_width - 1) {
    label = truncate_to(label, inner_width - 1);
    label_w = display_width(label);
  }
  top += label;
  int remaining = inner_width - 1 - label_w;  // -1 for the single h already added
  if (remaining < 0) remaining = 0;
  top += repeat(bc.h, remaining) + bc.tr;
  out.push_back(top);

  // Content lines, each padded/truncated to inner_width.
  for (const auto& ln : lines) {
    std::string content = truncate_to(ln, inner_width);
    content = pad_to(content, inner_width);
    out.push_back(std::string(bc.v) + content + bc.v);
  }

  // Bottom border.
  out.push_back(std::string(bc.bl) + repeat(bc.h, inner_width) + bc.br);
  return out;
}

std::string bar(double frac, int width, char fill_ascii) {
  frac = std::clamp(frac, 0.0, 1.0);
  int k = static_cast<int>(frac * width + 0.5);
  return std::string(static_cast<size_t>(k), fill_ascii) +
         std::string(static_cast<size_t>(width - k), ' ');
}

std::string block_bar(double frac, int width) {
  frac = std::clamp(frac, 0.0, 1.0);
  double total = frac * width;
  int full = static_cast<int>(total);
  std::string out = repeat("█", full);
  // fractional eighth
  static const std::array<const char*, 8> eighths = {
      "", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
  int frac8 = static_cast<int>((total - full) * 8);
  if (frac8 > 0 && full < width) {
    out += eighths[static_cast<size_t>(frac8)];
    full += 1;
  }
  out += std::string(static_cast<size_t>(std::max(0, width - full)), ' ');
  return out;
}

std::string sparkline(const std::vector<double>& values) {
  static const std::array<const char*, 8> ramp = {
      "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  if (values.empty()) return "";
  double lo = *std::min_element(values.begin(), values.end());
  double hi = *std::max_element(values.begin(), values.end());
  double rng = (hi - lo);
  if (rng <= 0) rng = 1.0;
  std::string out;
  for (double v : values) {
    int idx = static_cast<int>((v - lo) / rng * 7.0 + 0.5);
    idx = std::clamp(idx, 0, 7);
    out += ramp[static_cast<size_t>(idx)];
  }
  return out;
}

std::vector<std::string> beside(const std::vector<std::string>& left,
                                const std::vector<std::string>& right, int gap) {
  // TODO(tui): equalise column HEIGHT, not just width. When left/right blocks
  // differ in line count (corpus box is usually taller than keureval), the
  // shorter box ends higher and leaves ragged whitespace beside the taller one.
  // Pad the shorter block with blank lines to match, or continue its right
  // border. Cosmetic only; tracked in TODO.md. See cmd/learn.cpp draw_frame().
  int lw = 0;
  for (const auto& l : left) lw = std::max(lw, display_width(l));
  size_t rows = std::max(left.size(), right.size());
  std::vector<std::string> out;
  out.reserve(rows);
  const std::string gap_s(static_cast<size_t>(std::max(0, gap)), ' ');
  for (size_t i = 0; i < rows; ++i) {
    std::string l = (i < left.size()) ? pad_to(left[i], lw) : std::string(static_cast<size_t>(lw), ' ');
    std::string r = (i < right.size()) ? right[i] : "";
    out.push_back(l + gap_s + r);
  }
  return out;
}

}  // namespace keur::render
