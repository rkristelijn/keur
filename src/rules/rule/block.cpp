/**
 * @file rule/block.cpp
 * @brief block-absence engine (ADR-004 stage 2) — brace-scope matching.
 *
 * For each line matching the block-START pattern (patterns[0]), find the block's
 * end by counting braces from the first `{`, then flag the block when the
 * required `block_inner` regex never appears inside it. Optional `block_trigger`
 * gates the check (only evaluate blocks containing it), turning this into a
 * general "scope contains A but not B" detector (e.g. a function that opens a
 * file but never closes it; a test block with no assertion).
 *
 * The brace counter uses a single-line lexer that skips `{`/`}` inside strings
 * and regex literals, so literal braces do not skew the depth. Adopted from cpm
 * (adr-166 later additions); shaped to keur's RuleFinding.
 */
#include "rule/internal.h"

namespace keur::detail {

void eval_block_absence(const Rule& rule,
                        const RE2* start_re,
                        const RE2* inner_re,
                        const RE2* trigger_re,
                        const std::vector<re2::StringPiece>& lines,
                        const std::string& rel,
                        std::vector<RuleFinding>& findings) {
  if (!start_re || !inner_re) return;

  for (size_t li = 0; li < lines.size(); li++) {
    if (!RE2::PartialMatch(lines[li], *start_re)) continue;

    int depth = 0;
    bool opened = false;
    bool inner_found = false;
    bool trigger_found = (trigger_re == nullptr);  // no gate => always satisfied
    size_t end = li;

    for (size_t j = li; j < lines.size(); j++) {
      std::string_view lv(lines[j].data(), lines[j].size());
      char in_str = 0;
      bool in_regex = false;
      bool escaped = false;
      for (size_t k = 0; k < lv.size(); k++) {
        char c = lv[k];
        if (in_str) {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == in_str) in_str = 0;
          continue;
        }
        if (in_regex) {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == '/') in_regex = false;
          continue;
        }
        if (c == '\'' || c == '"' || c == '`') { in_str = c; continue; }
        // Heuristic regex-literal start: '/' preceded (ignoring spaces) by a
        // token that cannot end an expression, and not a // or /* comment.
        if (c == '/' && k + 1 < lv.size() && lv[k + 1] != '/' && lv[k + 1] != '*') {
          size_t p = k;
          while (p > 0 && (lv[p - 1] == ' ' || lv[p - 1] == '\t')) p--;
          char prev = p > 0 ? lv[p - 1] : '\0';
          if (prev == '\0' || prev == '(' || prev == ',' || prev == '=' ||
              prev == ':' || prev == '[' || prev == '!' || prev == '&' ||
              prev == '|' || prev == ';' || prev == '{' || prev == '?') {
            in_regex = true;
            continue;
          }
        }
        if (c == '{') { depth++; opened = true; }
        else if (c == '}') { depth--; }
      }
      if (RE2::PartialMatch(lines[j], *inner_re)) inner_found = true;
      if (trigger_re && RE2::PartialMatch(lines[j], *trigger_re)) trigger_found = true;
      end = j;
      if (opened && depth <= 0) break;
    }

    if (opened && trigger_found && !inner_found) {
      std::string msg = finding_msg(
          rule, rule.patterns.empty() ? std::string() : rule.patterns[0].message);
      findings.push_back({rule.id, rule.title, rule.category, rule.severity,
                          default_action(rule), default_conf(rule), rel,
                          static_cast<int>(li + 1), msg});
    }
    li = end;  // resume after this block (no stacked/nested reports)
  }
}

}  // namespace keur::detail
