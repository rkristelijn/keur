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
#include <vector>

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

/** Replace every emoji / pictographic-symbol codepoint with spaces, byte-for-
 *  byte (a 4-byte emoji becomes 4 spaces), so line and column offsets are
 *  preserved. Language-agnostic: operates on UTF-8 text, not source syntax.
 *  Covers the standard emoji blocks (Misc Symbols & Pictographs, Emoticons,
 *  Transport & Map, Supplemental & Extended-A, Misc Symbols, Dingbats) plus the
 *  variation selector U+FE0F and the zero-width joiner U+200D used in sequences.
 *  Enables the per-rule `skip_emoji` feature so a decorative `## ✨ Features`
 *  heading does not hide the word from a `^#+\s+(Features)` regex (ADR-061).
 *  @implements ADR-061 */
std::string strip_emoji(const std::string& content);

/** A single string literal extracted from source: its text (delimiters and
 *  escape backslashes removed) and the 1-based line where it opened. */
struct Literal {
  std::string text;  // the literal's content, without surrounding delimiters
  int line;          // 1-based line number where the literal started
};

/** Third projection (ADR-058): the inverse of strip_comments_and_strings.
 *  Comments are ignored; the CONTENT of each string literal is emitted as a
 *  Literal. Code (keywords, identifiers, operators) is dropped. Line numbers
 *  are 1-based and refer to where each literal opened. nullptr syntax (or a
 *  language with no string delimiters, e.g. markdown) yields no literals. */
std::vector<Literal> extract_literals(const std::string& content, const LangSyntax* syntax);

}  // namespace keur

#endif  // KEUR_UTIL_TOKENIZER_H
