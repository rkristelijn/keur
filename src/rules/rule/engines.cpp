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

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace keur::detail {

namespace {

// Emit one finding for rule r at (rel, line) with the given message.
void emit(const Rule& r, const std::string& rel, int line,
          const std::string& msg, std::vector<RuleFinding>& out) {
  out.push_back({r.id, r.title, r.category, r.severity, default_action(r),
                 default_conf(r), rel, line, msg});
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
  };
  auto it = registry.find(name);
  return it == registry.end() ? nullptr : it->second;
}

}  // namespace keur::detail
