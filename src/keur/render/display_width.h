/**
 * @file render/display_width.h
 * @brief Unicode-aware terminal display width calculation.
 *
 * Correctly handles emoji (width 2), CJK (width 2), zero-width joiners,
 * variation selectors, and box-drawing (width 1). This is the key to correct
 * box alignment: pad by DISPLAY columns, not bytes or char count.
 *
 * ---------------------------------------------------------------------------
 * Vendored from tuigram (https://github.com/rkristelijn/tuigram),
 * src/core/display_width.{h,cpp}. Licensed MIT:
 *
 *   MIT License — Copyright (c) 2026 Remi Kristelijn
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction... (full text: LICENSE-tuigram)
 *
 * Namespace changed tuigram:: -> keur::render:: for vendoring; logic unchanged.
 * ---------------------------------------------------------------------------
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace keur::render {

/// Display width of a single codepoint in terminal columns.
[[nodiscard]] constexpr int codepoint_width(uint32_t cp) {
  // Zero-width characters
  if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF ||
      cp == 0xFE0F || cp == 0xFE0E || (cp >= 0x1F3FB && cp <= 0x1F3FF)) {
    return 0;
  }
  // Emoji (width 2)
  if ((cp >= 0x1F300 && cp <= 0x1F9FF) || (cp >= 0x2600 && cp <= 0x27BF) ||
      (cp >= 0x1FA00 && cp <= 0x1FAFF) || (cp >= 0x231A && cp <= 0x23F3) ||
      cp == 0x274C || cp == 0x274E || cp == 0x2714 || cp == 0x2716) {
    return 2;
  }
  // CJK (width 2)
  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
      (cp >= 0x3040 && cp <= 0x33BF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
      (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF01 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x20000 && cp <= 0x2FFFD)) {
    return 2;
  }
  return 1;
}

/// Display width of a UTF-8 string in terminal columns.
[[nodiscard]] int display_width(std::string_view s);

/// Pad string with spaces to reach target display width (left-aligned).
[[nodiscard]] std::string pad_to(std::string_view s, int target);

/// Center string within target display width.
[[nodiscard]] std::string center_to(std::string_view s, int target);

/// Right-align string within target display width.
[[nodiscard]] std::string right_align_to(std::string_view s, int target);

/// Truncate string to fit within max display width.
[[nodiscard]] std::string truncate_to(std::string_view s, int max_width);

}  // namespace keur::render
