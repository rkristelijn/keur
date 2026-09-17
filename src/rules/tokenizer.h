/**
 * @file util/tokenizer.h
 * @brief Language-aware tokenizer — strips comments and strings from source.
 *
 * Single-pass O(n) state machine, zero external dependencies. Supports 15+
 * languages via a static syntax table. Line structure is preserved (comments/
 * strings become spaces, newlines kept) so line numbers stay correct.
 *
 * Enables rule features skip_comments / skip_strings: match the code, not the
 * commentary. (Fixes e.g. a rule firing on a dangerous-call keyword that only
 * appears inside a comment.)
 *
 * ---------------------------------------------------------------------------
 * Taken from cpm (https://github.com/rkristelijn/cpm) src/analysis/tokenizer.
 * keur's rule engine is a clean-room reimplementation of cpm's design; this
 * tokenizer is adopted verbatim into namespace keur (logic unchanged).
 * ---------------------------------------------------------------------------
 */
#ifndef KEUR_UTIL_TOKENIZER_H
#define KEUR_UTIL_TOKENIZER_H

#include <string>

namespace keur {

/** Syntax rules for a single language family. */
struct LangSyntax {
  const char* extensions[8];  // file extensions (e.g. ".cpp", ".h")
  const char* line_comment;   // "//" or "#" or "--" or nullptr
  const char* block_start;    // "/*" or "<!--" or nullptr
  const char* block_end;      // "*/" or "-->" or nullptr
  char string_delims[4];      // e.g. '"', '\'', '`', 0
  bool triple_strings;        // Python/Kotlin triple-quoted strings
};

/** Look up syntax for a file extension (with dot, e.g. ".py"). nullptr if unknown. */
const LangSyntax* lang_syntax(const std::string& extension);

/** Strip comments (replaced by spaces, newlines kept). Strings left intact.
 *  nullptr syntax returns content unchanged. */
std::string strip_comments(const std::string& content, const LangSyntax* syntax);

/** Strip both comments AND string literals, preserving line structure. */
std::string strip_comments_and_strings(const std::string& content, const LangSyntax* syntax);

}  // namespace keur

#endif  // KEUR_UTIL_TOKENIZER_H
