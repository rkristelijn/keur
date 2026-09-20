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

}  // namespace

// The registry. Adding an engine = one entry here (ADR-014).
EngineFn engine_lookup(const std::string& name) {
  static const std::unordered_map<std::string, EngineFn> registry = {
      {"pattern", &eng_pattern},
      {"presence", &eng_presence},
      {"absence", &eng_absence},
      {"block-absence", &eng_block_absence},
      {"count", &eng_count},
      {"multiline", &eng_multiline},
  };
  auto it = registry.find(name);
  return it == registry.end() ? nullptr : it->second;
}

}  // namespace keur::detail
