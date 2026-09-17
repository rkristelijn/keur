/**
 * @file util/tokenizer.cpp
 * @brief Language-aware tokenizer — single-pass state machine.
 *
 * Adopted from cpm (https://github.com/rkristelijn/cpm) src/analysis/tokenizer.cpp.
 * Wrapped in namespace keur; logic unchanged. See util/tokenizer.h.
 */
#include "tokenizer.h"

#include <cstring>

namespace keur {

// --- Syntax table: comment/string syntax per language family ---
static const LangSyntax SYNTAX_TABLE[] = {
    {{".c", ".cpp", ".h", ".hpp", ".java", ".cs", ".go", ".rs"}, "//", "/*", "*/", {'"', '\'', 0, 0}, false},
    {{".swift", ".kt", nullptr}, "//", "/*", "*/", {'"', '\'', 0, 0}, false},
    {{".js", ".ts", ".jsx", ".tsx", ".vue", ".mjs", nullptr}, "//", "/*", "*/", {'"', '\'', '`', 0}, false},
    {{".py", nullptr}, "#", nullptr, nullptr, {'"', '\'', 0, 0}, true},
    {{".rb", nullptr}, "#", "=begin", "=end", {'"', '\'', 0, 0}, false},
    {{".php", nullptr}, "//", "/*", "*/", {'"', '\'', 0, 0}, false},
    {{".sh", ".bash", nullptr}, "#", nullptr, nullptr, {'"', '\'', 0, 0}, false},
    {{".sql", nullptr}, "--", "/*", "*/", {'"', '\'', 0, 0}, false},
    {{".lua", nullptr}, "--", "--[[", "]]", {'"', '\'', 0, 0}, false},
    {{".html", ".htm", ".xml", ".svg", nullptr}, nullptr, "<!--", "-->", {'"', '\'', 0, 0}, false},
    {{".css", ".scss", ".less", nullptr}, nullptr, "/*", "*/", {'"', '\'', 0, 0}, false},
    {{".yml", ".yaml", nullptr}, "#", nullptr, nullptr, {'"', '\'', 0, 0}, false},
    {{".toml", nullptr}, "#", nullptr, nullptr, {'"', '\'', 0, 0}, false},
    {{".tf", ".hcl", ".tfvars", nullptr}, "#", "/*", "*/", {'"', 0, 0, 0}, false},
    {{".md", nullptr}, nullptr, nullptr, nullptr, {0, 0, 0, 0}, false},
};

static const int SYNTAX_COUNT = sizeof(SYNTAX_TABLE) / sizeof(SYNTAX_TABLE[0]);

const LangSyntax* lang_syntax(const std::string& extension) {
  if (extension.empty()) return nullptr;
  for (int i = 0; i < SYNTAX_COUNT; i++) {
    for (int j = 0; j < 8 && SYNTAX_TABLE[i].extensions[j]; j++) {
      if (extension == SYNTAX_TABLE[i].extensions[j]) return &SYNTAX_TABLE[i];
    }
  }
  return nullptr;
}

enum class State { NORMAL, LINE_COMMENT, BLOCK_COMMENT, STRING, TRIPLE_STRING };

static bool match_at(const std::string& s, size_t pos, const char* prefix) {
  if (!prefix) return false;
  size_t len = std::strlen(prefix);
  if (pos + len > s.size()) return false;
  return std::memcmp(s.data() + pos, prefix, len) == 0;
}

static bool is_string_delim(char c, const char delims[4]) {
  for (int i = 0; i < 4 && delims[i]; i++)
    if (c == delims[i]) return true;
  return false;
}

static std::string strip_impl(const std::string& content, const LangSyntax* syntax, bool strip_strings) {
  if (!syntax) return content;

  std::string out = content;
  const size_t len = content.size();

  State state = State::NORMAL;
  char string_delim = 0;
  size_t block_end_len = syntax->block_end ? std::strlen(syntax->block_end) : 0;
  size_t block_start_len = syntax->block_start ? std::strlen(syntax->block_start) : 0;
  size_t line_comment_len = syntax->line_comment ? std::strlen(syntax->line_comment) : 0;

  bool php_hash = false;
  for (int j = 0; j < 8 && syntax->extensions[j]; j++) {
    if (std::strcmp(syntax->extensions[j], ".php") == 0) { php_hash = true; break; }
  }

  size_t i = 0;
  while (i < len) {
    char c = content[i];
    switch (state) {
      case State::NORMAL: {
        if (syntax->triple_strings && i + 2 < len && is_string_delim(c, syntax->string_delims) &&
            content[i + 1] == c && content[i + 2] == c) {
          state = State::TRIPLE_STRING;
          string_delim = c;
          if (strip_strings) { out[i] = ' '; out[i + 1] = ' '; out[i + 2] = ' '; }
          i += 3;
          continue;
        }
        if (syntax->block_start && match_at(content, i, syntax->block_start)) {
          state = State::BLOCK_COMMENT;
          for (size_t k = 0; k < block_start_len; k++) out[i + k] = ' ';
          i += block_start_len;
          continue;
        }
        if (syntax->line_comment && match_at(content, i, syntax->line_comment)) {
          state = State::LINE_COMMENT;
          for (size_t k = 0; k < line_comment_len; k++) out[i + k] = ' ';
          i += line_comment_len;
          continue;
        }
        if (php_hash && c == '#') {
          state = State::LINE_COMMENT;
          out[i] = ' ';
          i++;
          continue;
        }
        if (is_string_delim(c, syntax->string_delims)) {
          state = State::STRING;
          string_delim = c;
          if (strip_strings) out[i] = ' ';
          i++;
          continue;
        }
        i++;
        break;
      }
      case State::LINE_COMMENT: {
        if (c == '\n') { state = State::NORMAL; i++; }
        else { out[i] = ' '; i++; }
        break;
      }
      case State::BLOCK_COMMENT: {
        if (match_at(content, i, syntax->block_end)) {
          for (size_t k = 0; k < block_end_len; k++) out[i + k] = ' ';
          i += block_end_len;
          state = State::NORMAL;
        } else {
          if (c != '\n') out[i] = ' ';
          i++;
        }
        break;
      }
      case State::STRING: {
        if (c == '\\') {
          if (strip_strings) out[i] = ' ';
          i++;
          if (i < len) {
            if (strip_strings && content[i] != '\n') out[i] = ' ';
            i++;
          }
        } else if (c == string_delim) {
          if (strip_strings) out[i] = ' ';
          i++;
          state = State::NORMAL;
        } else if (c == '\n') {
          state = State::NORMAL;
          i++;
        } else {
          if (strip_strings) out[i] = ' ';
          i++;
        }
        break;
      }
      case State::TRIPLE_STRING: {
        if (c == string_delim && i + 2 < len && content[i + 1] == string_delim && content[i + 2] == string_delim) {
          if (strip_strings) { out[i] = ' '; out[i + 1] = ' '; out[i + 2] = ' '; }
          i += 3;
          state = State::NORMAL;
        } else {
          if (strip_strings && c != '\n') out[i] = ' ';
          i++;
        }
        break;
      }
    }
  }
  return out;
}

std::string strip_comments(const std::string& content, const LangSyntax* syntax) {
  return strip_impl(content, syntax, false);
}

std::string strip_comments_and_strings(const std::string& content, const LangSyntax* syntax) {
  return strip_impl(content, syntax, true);
}

}  // namespace keur
