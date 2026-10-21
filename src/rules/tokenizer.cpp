/**
 * @file util/tokenizer.cpp
 * @brief Language-aware tokenizer — single-pass state machine.
 *
 * Adopted from cpm (https://github.com/rkristelijn/cpm) src/analysis/tokenizer.cpp.
 * Wrapped in namespace keur; logic unchanged. See util/tokenizer.h.
 * @implements ADR-004.stage1
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

// Is this Unicode codepoint an emoji / pictographic symbol we strip? Closed set
// of the standard emoji blocks plus the two sequence glue codepoints (variation
// selector FE0F, zero-width joiner 200D). Deliberately NOT stripping general
// punctuation or letters — only decorative pictographs.
static bool is_emoji_cp(unsigned int cp) {
  return (cp >= 0x1F300 && cp <= 0x1F5FF) ||  // Misc Symbols & Pictographs
         (cp >= 0x1F600 && cp <= 0x1F64F) ||  // Emoticons
         (cp >= 0x1F680 && cp <= 0x1F6FF) ||  // Transport & Map
         (cp >= 0x1F900 && cp <= 0x1F9FF) ||  // Supplemental Symbols & Pictographs
         (cp >= 0x1FA70 && cp <= 0x1FAFF) ||  // Symbols & Pictographs Extended-A
         (cp >= 0x2600  && cp <= 0x26FF)  ||  // Miscellaneous Symbols (☀ ⚠ ⚙)
         (cp >= 0x2700  && cp <= 0x27BF)  ||  // Dingbats (✨ ✅ ✔ ✈)
         cp == 0xFE0F   ||                    // variation selector-16 (emoji style)
         cp == 0x200D;                        // zero-width joiner (emoji sequences)
}

std::string strip_emoji(const std::string& content) {
  std::string out = content;
  const size_t len = content.size();
  size_t i = 0;
  while (i < len) {
    unsigned char b = (unsigned char)content[i];
    // Decode one UTF-8 scalar. ASCII fast-path keeps the common case cheap.
    unsigned int cp = 0;
    size_t n = 1;
    if (b < 0x80) { i += 1; continue; }
    else if ((b & 0xE0) == 0xC0 && i + 1 < len) { cp = b & 0x1F; n = 2; }
    else if ((b & 0xF0) == 0xE0 && i + 2 < len) { cp = b & 0x0F; n = 3; }
    else if ((b & 0xF8) == 0xF0 && i + 3 < len) { cp = b & 0x07; n = 4; }
    else { i += 1; continue; }  // invalid lead byte — leave as-is, advance one
    bool ok = true;
    for (size_t k = 1; k < n; k++) {
      unsigned char cont = (unsigned char)content[i + k];
      if ((cont & 0xC0) != 0x80) { ok = false; break; }  // not a continuation byte
      cp = (cp << 6) | (cont & 0x3F);
    }
    if (!ok) { i += 1; continue; }
    if (is_emoji_cp(cp))
      for (size_t k = 0; k < n; k++) out[i + k] = ' ';  // blank, byte-for-byte
    i += n;
  }
  return out;
}

// --- Literal extraction (ADR-058): the inverse projection ---
// Same single-pass state machine as strip_impl, but instead of blanking string
// content it collects it. Comments are skipped (never collected). One Literal
// per string, tagged with the 1-based line where it opened.
std::vector<Literal> extract_literals(const std::string& content, const LangSyntax* syntax) {
  std::vector<Literal> out;
  if (!syntax) return out;

  const size_t len = content.size();
  size_t block_end_len = syntax->block_end ? std::strlen(syntax->block_end) : 0;
  size_t block_start_len = syntax->block_start ? std::strlen(syntax->block_start) : 0;
  size_t line_comment_len = syntax->line_comment ? std::strlen(syntax->line_comment) : 0;

  bool php_hash = false;
  for (int j = 0; j < 8 && syntax->extensions[j]; j++) {
    if (std::strcmp(syntax->extensions[j], ".php") == 0) { php_hash = true; break; }
  }

  State state = State::NORMAL;
  char string_delim = 0;
  int line = 1;              // 1-based current line
  std::string cur;           // accumulator for the literal being read
  int cur_line = 0;          // line where the current literal opened

  auto flush = [&]() {
    // Emit the accumulated literal (even empty ones are dropped — an empty
    // string carries no identifier signal and only adds noise).
    if (!cur.empty()) out.push_back(Literal{cur, cur_line});
    cur.clear();
  };

  size_t i = 0;
  while (i < len) {
    char c = content[i];
    switch (state) {
      case State::NORMAL: {
        if (syntax->triple_strings && i + 2 < len && is_string_delim(c, syntax->string_delims) &&
            content[i + 1] == c && content[i + 2] == c) {
          state = State::TRIPLE_STRING;
          string_delim = c;
          cur_line = line;
          i += 3;
          continue;
        }
        if (syntax->block_start && match_at(content, i, syntax->block_start)) {
          state = State::BLOCK_COMMENT;
          i += block_start_len;
          continue;
        }
        if (syntax->line_comment && match_at(content, i, syntax->line_comment)) {
          state = State::LINE_COMMENT;
          i += line_comment_len;
          continue;
        }
        if (php_hash && c == '#') { state = State::LINE_COMMENT; i++; continue; }
        if (is_string_delim(c, syntax->string_delims)) {
          state = State::STRING;
          string_delim = c;
          cur_line = line;
          i++;
          continue;
        }
        if (c == '\n') line++;
        i++;
        break;
      }
      case State::LINE_COMMENT: {
        if (c == '\n') { state = State::NORMAL; line++; }
        i++;
        break;
      }
      case State::BLOCK_COMMENT: {
        if (match_at(content, i, syntax->block_end)) {
          i += block_end_len;
          state = State::NORMAL;
        } else {
          if (c == '\n') line++;
          i++;
        }
        break;
      }
      case State::STRING: {
        if (c == '\\') {
          i++;  // skip the escape marker
          if (i < len) {
            if (content[i] == '\n') line++;
            else cur.push_back(content[i]);  // keep the escaped char's value
            i++;
          }
        } else if (c == string_delim) {
          flush();
          state = State::NORMAL;
          i++;
        } else if (c == '\n') {
          // Unterminated single-line string: close it, count the newline.
          flush();
          state = State::NORMAL;
          line++;
          i++;
        } else {
          cur.push_back(c);
          i++;
        }
        break;
      }
      case State::TRIPLE_STRING: {
        if (c == string_delim && i + 2 < len && content[i + 1] == string_delim && content[i + 2] == string_delim) {
          flush();
          i += 3;
          state = State::NORMAL;
        } else {
          if (c == '\n') line++;
          cur.push_back(c);
          i++;
        }
        break;
      }
    }
  }
  // EOF inside a string: emit whatever we have.
  if (state == State::STRING || state == State::TRIPLE_STRING) flush();
  return out;
}

}  // namespace keur
