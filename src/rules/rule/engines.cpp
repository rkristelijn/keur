/**
 * @file rule/engines.cpp
 * @brief Per-line engines as a strategy registry (ADR-014).
 * @implements ADR-014
 *
 * Each engine is a small function over a ScanContext, registered by name. The
 * scan loop (scan.cpp) looks one up instead of an if/else chain, so adding an
 * engine is one entry here — the scan loop does not change. Behaviour is
 * identical to the former inline dispatch; this is a pure structural refactor.
 *
 * File-level engines (file-absence/file-presence) run once over the whole
 * fileset in scan.cpp's eval_file_level_rules and are intentionally not here.
 */
#include "rule/internal.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace keur::detail {

namespace {

// Emit one finding for rule r at (rel, line) with the given message.
void emit(const Rule& r, const std::string& rel, int line,
          const std::string& msg, std::vector<RuleFinding>& out) {
  out.push_back({r.id, r.title, r.category, r.severity, default_action(r),
                 default_conf(r), rel, line, msg, r.fix});
}

// pattern: flag EACH line matching a pattern; pattern-not (exclude) suppresses
// the safe case on the same line. @id engine-pattern
void eng_pattern(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  for (size_t pi = 0; pi < c.res.size(); pi++) {
    if (!c.res[pi]) continue;
    const RE2* excl = pi < c.exclude_res.size() ? c.exclude_res[pi].get() : nullptr;
    for (size_t li = 0; li < cx.lines.size(); li++)
      if (RE2::PartialMatch(cx.lines[li], *c.res[pi])) {
        if (excl && RE2::PartialMatch(cx.lines[li], *excl)) continue;
        emit(r, cx.rel, (int)(li + 1), finding_msg(r, r.patterns[pi].message), out);
      }
  }
}

// presence: flag a file where a pattern DOES appear — at most one per pattern.
// @id engine-presence
void eng_presence(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  for (size_t pi = 0; pi < c.res.size(); pi++) {
    if (!c.res[pi]) continue;
    for (size_t li = 0; li < cx.lines.size(); li++)
      if (RE2::PartialMatch(cx.lines[li], *c.res[pi])) {
        emit(r, cx.rel, (int)(li + 1), finding_msg(r, r.patterns[pi].message), out);
        break;
      }
  }
}

// absence: flag a file where a required pattern NEVER appears. @id engine-absence
void eng_absence(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  for (size_t pi = 0; pi < c.res.size(); pi++) {
    if (!c.res[pi]) continue;
    bool found = false;
    for (size_t li = 0; li < cx.lines.size() && !found; li++)
      if (RE2::PartialMatch(cx.lines[li], *c.res[pi])) found = true;
    if (!found)
      emit(r, cx.rel, 1, finding_msg(r, r.patterns[pi].message), out);
  }
}

// block-absence: delegate to the brace-scope evaluator. @id engine-block-absence
void eng_block_absence(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  eval_block_absence(*c.rule, c.res.empty() ? nullptr : c.res[0].get(),
                     c.block_inner.get(), c.block_trigger.get(), cx.lines, cx.rel, out);
}

// count: flag ONCE when the pattern matches >= min_count times within the file.
// Reusable "too many of X": long dispatch chains, too many imports, etc. The
// finding points at the line of the match that crosses the threshold.
// @id engine-count
void eng_count(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  const int threshold = r.min_count > 0 ? r.min_count : 2;  // sane default
  if (c.res.empty() || !c.res[0]) return;
  const RE2& re = *c.res[0];
  int hits = 0;
  for (size_t li = 0; li < cx.lines.size(); li++) {
    if (RE2::PartialMatch(cx.lines[li], re)) {
      if (++hits == threshold) {   // fire exactly once, at the crossing line
        emit(r, cx.rel, (int)(li + 1), finding_msg(r, r.patterns[0].message), out);
      }
    }
  }
}

// multiline: match a pattern that SPANS lines, bounded to a sliding window of
// `window` lines (default 2). RE2 is per-line; we join a window with newlines
// and match once over the join, so `[\s\S]*?` / `(?s).` can cross lines WITHOUT
// a file-wide match (which would flood findings — see rules-authoring.md). The
// finding is reported at the window's START line so it points at a real place
// and inline suppression still works. After a hit we advance past the window so
// one logical span is reported once, not once per overlapping start. @id engine-multiline
void eng_multiline(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  if (c.res.empty() || !c.res[0]) return;
  const RE2& re = *c.res[0];
  const RE2* excl = c.exclude_res.empty() ? nullptr : c.exclude_res[0].get();
  const int win = r.window > 0 ? r.window : 2;  // sane default: two lines
  const size_t n = cx.lines.size();
  for (size_t li = 0; li < n; li++) {
    // Build the window [li, li+win) joined by newlines so the pattern can span
    // lines but never beyond `win` lines.
    std::string blob;
    size_t hi = li + (size_t)win < n ? li + (size_t)win : n;
    for (size_t j = li; j < hi; j++) {
      if (j > li) blob.push_back('\n');
      blob.append(cx.lines[j].data(), cx.lines[j].size());
    }
    if (RE2::PartialMatch(blob, re)) {
      // pattern-not (exclude): if the window also matches the exclude regex,
      // this is a negative case (e.g. the sanitising call is present) — skip.
      if (excl && RE2::PartialMatch(blob, *excl)) { li = hi - 1; continue; }
      emit(r, cx.rel, (int)(li + 1), finding_msg(r, r.patterns[0].message), out);
      li = hi - 1;  // advance past this window (loop's ++ moves to hi)
    }
  }
}

// path-derived: the expected string is COMPUTED from the file's path, not a
// constant. `path_capture` (regex with capture groups) is applied to cx.rel; if
// it matches, \1.. are substituted into `derived_template` to build the string
// this file must contain. Absent → flag. Lets ONE rule express "a file in
// src/keur/<layer>/ must declare namespace keur::<layer>" for every layer, with
// no per-layer edit (ADR-020). RE2-safe: ordinary capture, no backrefs. The
// derived string is matched as literal text (not regex), so it is ReDoS-free.
// @id engine-path-derived
void eng_path_derived(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Rule& r = *cx.compiled.rule;
  if (r.path_capture.empty() || r.derived_template.empty()) return;

  RE2 cap(r.path_capture);
  if (!cap.ok()) return;

  // Extract up to 4 capture groups from the path. A file whose path does not
  // match is simply out of scope for this rule (no finding).
  std::string g1, g2, g3, g4;
  const int n = cap.NumberOfCapturingGroups();
  RE2::Arg a1(&g1), a2(&g2), a3(&g3), a4(&g4);
  const RE2::Arg* args[] = {&a1, &a2, &a3, &a4};
  if (!RE2::PartialMatchN(cx.rel, cap, args, n > 4 ? 4 : (n < 0 ? 0 : n))) return;

  // Build the expected literal by substituting \1..\4 into the template.
  std::string expected = r.derived_template;
  auto sub = [&expected](const std::string& ph, const std::string& val) {
    for (size_t p = expected.find(ph); p != std::string::npos; p = expected.find(ph, p))
      expected.replace(p, ph.size(), val);
  };
  sub("\\1", g1); sub("\\2", g2); sub("\\3", g3); sub("\\4", g4);
  if (expected.empty()) return;

  // Present-or-flag: pass if any non-suppressed line contains the literal.
  for (const auto& ln : cx.lines) {
    std::string_view sv(ln.data(), ln.size());
    if (sv.find(expected) != std::string_view::npos) return;
  }
  std::string msg = r.patterns.empty() ? std::string() : r.patterns[0].message;
  emit(r, cx.rel, 1, finding_msg(r, msg), out);
}

// max-nesting: flag a function whose CONTROL-FLOW nesting depth exceeds
// max_count (min_count field; default 3, matching SonarQube S134). Reuses the
// string/regex-aware brace lexer from block.cpp so literal braces in strings do
// not skew depth, and only counts braces opened by a control keyword
// (if/else/for/while/switch/do) — a struct/lambda/initializer brace does not
// raise the control depth. @id engine-max-nesting
void eng_max_nesting(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Rule& r = *cx.compiled.rule;
  const int limit = r.min_count > 0 ? r.min_count : 3;
  eval_max_nesting(r, limit, cx.lines, cx.rel, out);
}

// max-lines: flag a file whose NET line count (non-blank lines; comment lines
// are already stripped when the rule sets skip_comments) exceeds max_count
// (min_count field; default 300). Fires once per file, at line 1. Replaces the
// per-file size check in scripts/check-basics.sh with a real rule. A file-size
// metric is a per-file property, so it fits a plain line engine — no lexer,
// no cross-file state. @id engine-max-lines
void eng_max_lines(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Rule& r = *cx.compiled.rule;
  const int limit = r.min_count > 0 ? r.min_count : 300;
  int net = 0;
  for (const auto& ln : cx.lines) {
    std::string_view sv(ln.data(), ln.size());
    // Non-blank = has a non-whitespace char. (Comments are already stripped to
    // blanks upstream when skip_comments is set.)
    if (sv.find_first_not_of(" \t\r") != std::string_view::npos) net++;
  }
  if (net > limit)
    emit(r, cx.rel, 1, finding_msg(r, r.patterns.empty() ? std::string() : r.patterns[0].message), out);
}

// extract-duplicates: per file, apply the `extract` regex (with a capture group)
// to each line, collect the captured value, and flag the SECOND+ occurrence of
// the same value — an in-file duplicate (duplicate test name, route path,
// heading). A seen-set gives O(n). `{match}` in the message is replaced with the
// duplicated value. @id engine-extract-duplicates @implements ADR-004.stage2
void eng_extract_duplicates(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const Rule& r = *cx.compiled.rule;
  if (r.extract.empty()) return;
  RE2 re(r.extract);
  if (!re.ok()) return;
  const int grp = r.capture > 0 ? r.capture : 1;

  // cpm's extract-duplicates rules carry a top-level `message:` (with {match}),
  // not a patterns[] block; keur's own may use either. Prefer the rule-level
  // message, fall back to the first pattern's.
  std::string msg_tmpl = !r.message.empty()
      ? r.message
      : (r.patterns.empty() ? std::string() : r.patterns[0].message);

  std::unordered_set<std::string> seen;
  for (size_t li = 0; li < cx.lines.size(); li++) {
    // Extract up to `grp` groups; we only keep the requested one.
    std::string g1, g2, g3;
    RE2::Arg a1(&g1), a2(&g2), a3(&g3);
    const RE2::Arg* args[] = {&a1, &a2, &a3};
    int n = re.NumberOfCapturingGroups();
    if (n < grp) continue;                 // rule asks for a group the regex lacks
    if (!RE2::PartialMatchN(cx.lines[li], re, args, n > 3 ? 3 : n)) continue;
    const std::string& val = grp == 1 ? g1 : (grp == 2 ? g2 : g3);
    if (val.empty()) continue;
    if (!seen.insert(val).second) {        // already seen → this is a duplicate
      std::string msg = msg_tmpl;
      // Substitute {match} with the duplicated value.
      for (size_t p = msg.find("{match}"); p != std::string::npos; p = msg.find("{match}", p))
        msg.replace(p, 7, val);
      emit(r, cx.rel, (int)(li + 1), finding_msg(r, msg), out);
    }
  }
}

// --- base64 unwrap-then-scan (ADR-049) --------------------------------------
//
// A deterministic transform engine: find base64 blobs on a line, decode ONE
// layer, and re-run THIS rule's own patterns over the decoded bytes. The
// detection lives in the rule's patterns[] (data); the engine only performs the
// decode transform (ADR-009: the RE2 line engine stays dumb; a regex must not
// base64-decode). This closes the demonstrated blind spot where a secret/PII is
// one base64 layer deep and a surface scan matches only the encoded shape
// (motivating case: a cloudflared tunnel token committed as base64 JSON).
//
// Scope & limits (ADR-049): one decode layer; UTF-8/text output only (binary
// blobs — images, hashes, compiled bytes — are dropped); candidate length floor
// to keep the decode set small and deterministic. No network, no recursion.
//
// The finding points at the ENCODED span (the line the blob is on), never the
// decoded inner value — we do not echo the secret (consistent with ADR-042/046).
// @id engine-base64-unwrap @implements ADR-049

namespace {

// Decode a base64 / base64url token (padding optional). Returns false if the
// token is not valid base64 or decodes to bytes we should not re-scan.
bool b64_decode_one_layer(std::string_view in, std::string& out) {
  // Reverse lookup for the standard and URL-safe alphabets. '-'/'_' map to the
  // same values as '+'/'/' so base64url is handled without a second pass.
  static const auto make_table = [] {
    std::array<int8_t, 256> t{};
    t.fill(-1);
    const char* std_alpha =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) t[(unsigned char)std_alpha[i]] = (int8_t)i;
    t[(unsigned char)'-'] = 62;  // base64url
    t[(unsigned char)'_'] = 63;  // base64url
    return t;
  };
  static const std::array<int8_t, 256> T = make_table();

  // Strip trailing '=' padding; count only alphabet characters.
  std::string sym;
  sym.reserve(in.size());
  for (char ch : in) {
    if (ch == '=') break;  // padding — stop; anything after is not data
    int8_t v = T[(unsigned char)ch];
    if (v < 0) return false;  // not a base64 char → not a clean blob, bail
    sym.push_back(ch);
  }
  if (sym.size() < 4) return false;  // too short to carry anything

  out.clear();
  out.reserve(sym.size() * 3 / 4);
  uint32_t buf = 0;
  int bits = 0;
  for (char ch : sym) {
    buf = (buf << 6) | (uint32_t)T[(unsigned char)ch];
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((char)((buf >> bits) & 0xFF));
    }
  }
  return !out.empty();
}

// Is `s` valid UTF-8 AND does it look like text (no NUL/control bytes other than
// tab/newline/CR)? ADR-049 re-scans only text; binary decodes are dropped so we
// do not flood on images/hashes/compiled blobs.
bool looks_like_utf8_text(const std::string& s) {
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
      // ASCII; reject control chars except tab/newline/CR.
      if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
      i++;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= n || ((unsigned char)s[i + 1] & 0xC0) != 0x80) return false;
      if (c < 0xC2) return false;  // overlong
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= n || ((unsigned char)s[i + 1] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 2] & 0xC0) != 0x80) return false;
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= n || ((unsigned char)s[i + 1] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 2] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 3] & 0xC0) != 0x80) return false;
      i += 4;
    } else {
      return false;  // invalid lead byte
    }
  }
  return true;
}

// Percent-decode one layer: %XX -> byte, '+' -> space (form-encoding). Returns
// false if the input carries no percent-escape at all (nothing to unwrap) or a
// malformed escape. @implements ADR-049 (url variant)
bool url_decode_one_layer(std::string_view in, std::string& out) {
  auto hexval = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  out.clear();
  out.reserve(in.size());
  bool saw_escape = false;
  for (size_t i = 0; i < in.size(); i++) {
    char ch = in[i];
    if (ch == '%') {
      if (i + 2 >= in.size()) return false;
      int hi = hexval(in[i + 1]), lo = hexval(in[i + 2]);
      if (hi < 0 || lo < 0) return false;
      out.push_back((char)((hi << 4) | lo));
      i += 2;
      saw_escape = true;
    } else if (ch == '+') {
      out.push_back(' ');
    } else {
      out.push_back(ch);
    }
  }
  return saw_escape && !out.empty();  // require at least one real escape
}

// Decode a run of hex digits (even length) to bytes. Returns false if the input
// is not pure even-length hex. @implements ADR-049 (hex variant)
bool hex_decode_one_layer(std::string_view in, std::string& out) {
  if (in.size() < 2 || (in.size() % 2) != 0) return false;
  auto hexval = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  out.clear();
  out.reserve(in.size() / 2);
  for (size_t i = 0; i + 1 < in.size(); i += 2) {
    int hi = hexval(in[i]), lo = hexval(in[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back((char)((hi << 4) | lo));
  }
  return !out.empty();
}

}  // namespace (base64 helpers)

// Shared unwrap-then-scan driver (ADR-049). For each line, walk candidate runs
// (characters for which `is_cand` is true), and for every run of length >=
// `floor`, decode one layer with `decode`, drop non-UTF-8/binary output, then
// re-scan the decoded TEXT with the rule's own patterns. A hit is reported at
// the ENCODED line; the decoded value is never echoed (message comes from the
// pattern). This is the common body behind the base64/url/hex unwrap engines —
// only the alphabet and the decode step differ. @id engine-unwrap-driver
template <typename IsCand, typename Decode>
void unwrap_scan(const ScanContext& cx, int floor, IsCand is_cand, Decode decode,
                 std::vector<RuleFinding>& out) {
  const Compiled& c = cx.compiled;
  const Rule& r = *c.rule;
  if (c.res.empty()) return;  // nothing to re-scan with

  for (size_t li = 0; li < cx.lines.size(); li++) {
    std::string_view line(cx.lines[li].data(), cx.lines[li].size());
    size_t pos = 0, n = line.size();
    while (pos < n) {
      if (!is_cand(line[pos])) { pos++; continue; }
      size_t start = pos;
      while (pos < n && is_cand(line[pos])) pos++;
      std::string_view token = line.substr(start, pos - start);
      if ((int)token.size() < floor) continue;

      std::string decoded;
      if (!decode(token, decoded)) continue;
      if (!looks_like_utf8_text(decoded)) continue;  // ADR-049: text only

      bool fired = false;
      for (size_t pi = 0; pi < c.res.size() && !fired; pi++) {
        if (!c.res[pi]) continue;
        if (RE2::PartialMatch(decoded, *c.res[pi])) {
          emit(r, cx.rel, (int)(li + 1), finding_msg(r, r.patterns[pi].message), out);
          fired = true;  // one finding per encoded line is enough
        }
      }
    }
  }
}

// base64-unwrap: decode one base64/base64url layer, re-scan the decoded text.
// Candidate = base64 alphabet; default length floor 24. @id engine-base64-unwrap @implements ADR-049
void eng_base64_unwrap(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const int floor = cx.compiled.rule->min_count > 0 ? cx.compiled.rule->min_count : 24;
  unwrap_scan(cx, floor,
      [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' ||
               ch == '-' || ch == '_' || ch == '=';
      },
      b64_decode_one_layer, out);
}

// urldecode-unwrap: percent-decode one layer, re-scan the decoded text. The
// candidate run includes %XX escapes and the surrounding unreserved chars so a
// value like name%3Dsecret%40v is decoded as a whole. Default floor 8 (a short
// escaped value still matters). @id engine-urldecode-unwrap @implements ADR-049
void eng_urldecode_unwrap(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const int floor = cx.compiled.rule->min_count > 0 ? cx.compiled.rule->min_count : 8;
  unwrap_scan(cx, floor,
      [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '%' || ch == '+' ||
               ch == '-' || ch == '_' || ch == '.' || ch == '~';
      },
      url_decode_one_layer, out);
}

// hex-unwrap: decode a run of hex digits to bytes, re-scan the decoded text.
// Catches a bare hex-encoded blob (not a \xNN sequence — SCA-066 covers those).
// Default floor 16 (8 bytes). @id engine-hex-unwrap @implements ADR-049
void eng_hex_unwrap(const ScanContext& cx, std::vector<RuleFinding>& out) {
  const int floor = cx.compiled.rule->min_count > 0 ? cx.compiled.rule->min_count : 16;
  unwrap_scan(cx, floor,
      [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
               (ch >= 'A' && ch <= 'F');
      },
      hex_decode_one_layer, out);
}

}  // namespace
EngineFn engine_lookup(const std::string& name) {
  static const std::unordered_map<std::string, EngineFn> registry = {
      {"pattern", &eng_pattern},
      {"presence", &eng_presence},
      {"absence", &eng_absence},
      {"block-absence", &eng_block_absence},
      {"count", &eng_count},
      {"multiline", &eng_multiline},
      {"path-derived", &eng_path_derived},
      {"max-nesting", &eng_max_nesting},
      {"max-lines", &eng_max_lines},
      {"extract-duplicates", &eng_extract_duplicates},
      {"base64-unwrap", &eng_base64_unwrap},
      {"urldecode-unwrap", &eng_urldecode_unwrap},
      {"hex-unwrap", &eng_hex_unwrap},
  };
  auto it = registry.find(name);
  return it == registry.end() ? nullptr : it->second;
}

}  // namespace keur::detail
