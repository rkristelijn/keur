/**
 * @file rule/block.cpp
 * @brief block-absence engine (ADR-004 stage 2) — brace-scope matching.
 * @implements ADR-004.stage2
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
      findings.emplace_back(rule.id, rule.title, rule.category, rule.severity,
                          default_action(rule), default_conf(rule), rel,
                          static_cast<int>(li + 1), msg);
    }
    li = end;  // resume after this block (no stacked/nested reports)
  }
}

// --- max-nesting -----------------------------------------------------------
// Flag when CONTROL-FLOW nesting depth exceeds `limit`. We walk the whole file
// with the same string/regex-aware lexer used above so braces inside
// strings/regex/char-literals never skew the count. Only braces opened by a
// control keyword (if/else/for/while/switch/do) raise the control depth — a
// namespace/class/struct/function/lambda/initializer brace balances the raw
// scope but does NOT add control depth. The finding fires once per crossing
// (the line where control depth first exceeds the limit), then suppresses
// re-fires until the depth drops back to the limit, so one over-deep region is
// reported once. Matches SonarQube S134's intent (control nesting, not braces).
void eval_max_nesting(const Rule& rule, int limit,
                      const std::vector<re2::StringPiece>& lines,
                      const std::string& rel,
                      std::vector<RuleFinding>& findings) {
  auto is_control_kw = [](const std::string& w) {
    return w == "if" || w == "else" || w == "for" || w == "while" ||
           w == "switch" || w == "do";
  };

  int control_depth = 0;       // only control-keyword scopes
  bool reported = false;       // fired for the current over-deep region
  std::vector<bool> is_ctrl;   // per open brace: did it add control depth?
  int paren_depth = 0;         // inside (...) — e.g. a for(;;) header

  char in_str = 0;
  bool in_regex = false;
  bool escaped = false;
  std::string prev_word;
  bool pending_ctrl = false;   // last identifier was a control keyword

  for (size_t j = 0; j < lines.size(); j++) {
    std::string_view lv(lines[j].data(), lines[j].size());
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

      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
          (c >= '0' && c <= '9')) {
        prev_word.push_back(c);
        continue;
      }
      if (!prev_word.empty()) {
        if (is_control_kw(prev_word)) pending_ctrl = true;
        prev_word.clear();
      }

      // Track parenthesis depth so semicolons inside a `for (;;)` header do
      // not count as statement terminators.
      if (c == '(') { paren_depth++; continue; }
      if (c == ')') { if (paren_depth > 0) paren_depth--; continue; }

      // A top-level statement terminator (outside any (...)) means a control
      // keyword's body was a single statement (no brace) — clear the pending
      // flag so it can't attach to an unrelated later `{`. This is what makes
      // the heuristic reliable on real code (e.g. `if (x) return;`).
      if (c == ';' && paren_depth == 0) { pending_ctrl = false; continue; }

      if (c == '{') {
        bool ctrl = pending_ctrl;
        pending_ctrl = false;
        if (ctrl) {
          control_depth++;
          if (control_depth > limit && !reported) {
            std::string msg = finding_msg(
                rule, rule.patterns.empty() ? std::string() : rule.patterns[0].message);
            findings.emplace_back(rule.id, rule.title, rule.category, rule.severity,
                                default_action(rule), default_conf(rule), rel,
                                (int)(j + 1), msg);
            reported = true;
          }
        }
        is_ctrl.push_back(ctrl);
      } else if (c == '}') {
        if (!is_ctrl.empty()) {
          if (is_ctrl.back()) control_depth--;
          is_ctrl.pop_back();
        }
        if (control_depth <= limit) reported = false;  // re-arm once shallow again
      } else if (c == '/' && k + 1 < lv.size() && lv[k + 1] != '/' && lv[k + 1] != '*') {
        size_t p = k;
        while (p > 0 && (lv[p - 1] == ' ' || lv[p - 1] == '\t')) p--;
        char prev = p > 0 ? lv[p - 1] : '\0';
        if (prev == '\0' || prev == '(' || prev == ',' || prev == '=' ||
            prev == ':' || prev == '[' || prev == '!' || prev == '&' ||
            prev == '|' || prev == ';' || prev == '{' || prev == '?') {
          in_regex = true;
        }
      }
    }
    prev_word.clear();  // a keyword and its `{` may still be on separate lines
  }
}

}  // namespace keur::detail
