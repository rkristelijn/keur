/**
 * @file render/display_width.cpp
 * @brief Unicode-aware terminal display width — implementation.
 *
 * Vendored from tuigram (https://github.com/rkristelijn/tuigram),
 * src/core/display_width.cpp. Licensed MIT — Copyright (c) 2026 Remi Kristelijn.
 * See render/display_width.h and LICENSE-tuigram. Namespace changed to
 * keur::render; logic unchanged.
 */
#include "render/display_width.h"

namespace keur::render {

namespace {

/// Decode one UTF-8 codepoint, advance pointer. Returns 0xFFFD on error.
uint32_t decode_utf8(const char*& p, const char* end) {
  if (p >= end) return 0;
  auto c = static_cast<unsigned char>(*p);
  uint32_t cp = 0;
  int bytes = 0;

  if (c < 0x80) {
    cp = c;
    bytes = 1;
  } else if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    bytes = 2;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    bytes = 3;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07;
    bytes = 4;
  } else {
    p++;
    return 0xFFFD;  // replacement char
  }

  for (int i = 1; i < bytes && p + i < end; i++) {
    cp = (cp << 6) | (static_cast<unsigned char>(p[i]) & 0x3F);
  }
  p += bytes;
  return cp;
}

}  // namespace

int display_width(std::string_view s) {
  int width = 0;
  const char* p = s.data();
  const char* end = p + s.size();

  while (p < end) {
    uint32_t cp = decode_utf8(p, end);
    width += codepoint_width(cp);
  }
  return width;
}

std::string pad_to(std::string_view s, int target) {
  int current = display_width(s);
  if (current >= target) return std::string(s);
  return std::string(s) + std::string(static_cast<size_t>(target - current), ' ');
}

std::string center_to(std::string_view s, int target) {
  int current = display_width(s);
  if (current >= target) return std::string(s);
  int total_pad = target - current;
  int left_pad = total_pad / 2;
  int right_pad = total_pad - left_pad;
  return std::string(static_cast<size_t>(left_pad), ' ') + std::string(s) +
         std::string(static_cast<size_t>(right_pad), ' ');
}

std::string right_align_to(std::string_view s, int target) {
  int current = display_width(s);
  if (current >= target) return std::string(s);
  return std::string(static_cast<size_t>(target - current), ' ') + std::string(s);
}

std::string truncate_to(std::string_view s, int max_width) {
  std::string result;
  int width = 0;
  const char* p = s.data();
  const char* end = p + s.size();

  while (p < end) {
    const char* start = p;
    uint32_t cp = decode_utf8(p, end);
    int w = codepoint_width(cp);
    if (width + w > max_width) break;
    result.append(start, static_cast<size_t>(p - start));
    width += w;
  }
  return result;
}

}  // namespace keur::render
