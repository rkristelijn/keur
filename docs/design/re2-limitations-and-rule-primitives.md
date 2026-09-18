# RE2 limitations and KEUR rule primitives

> **Date**: 2026-09-18 · **Why**: writing KEUR-SEC-005 hit RE2's "no lookahead" wall. This maps
> every RE2 limitation that matters for rule-writing to what KEUR already offers, and flags the
> gaps worth a primitive (so we extend the engine deliberately, never reach for PCRE).
>
> **Guiding principle**: keep ONE engine (RE2). RE2's linear-time guarantee (no catastrophic
> backtracking / ReDoS) is a feature for a scanner over untrusted code. We add small, RE2-friendly
> rule primitives instead of swapping to a backtracking engine. @see ADR-001, ADR-007.

## RE2 feature limitations (vs PCRE) and how KEUR covers them

| RE2 can't do | Typical PCRE use | KEUR primitive today | Gap? |
|--------------|------------------|----------------------|------|
| **Lookahead** `(?=)` / `(?!)` | "match X except when Y on the line" | **`exclude:` per pattern** (ADR-007) — pattern-not / allowlist regex | Covered (line-scoped) |
| **Lookbehind** `(?<=)` / `(?<!)` | "match X only if preceded by Y" | Rewrite positively, or `exclude:` for the negative case | Mostly covered; positive-lookbehind sometimes awkward |
| **Backreferences** `\1` | "same token twice" (e.g. matching quotes/tags) | none | Rare in security rules; **accept the gap** (backrefs are the main ReDoS vector — do NOT add) |
| **Atomic groups / possessive** | perf tuning against backtracking | n/a — RE2 is already linear | Not needed |
| **Recursion / balanced groups** | match nested braces/parens | **`block-absence` engine** (brace-scoped block scan) | Covered for the "block" case; no general nesting |
| **Conditionals** `(?(1)a|b)` | branch on a prior group | none | Not seen in the corpus; **accept the gap** |
| **`\K`** (reset match start) | trim the match | capture-group emit (engine reports the line, not the span) | Line-level findings make this moot |
| **Named groups / captures in output** | extract the offending token into the message | none — messages are static | **Possible gap**: richer messages could quote the match. Low priority |

## Cross-cutting matching concerns (not RE2 features, but rule-writing needs)

| Need | KEUR primitive | Status |
|------|----------------|--------|
| Don't match inside **comments** | `skip_comments: true` (ADR-004) | Covered — used by SEC-005 to avoid flagging its own docstring |
| Don't match inside **string literals** | `skip_strings: true` | Covered — but strips the `"..."`, so a rule that *needs* a literal (e.g. concat detection) must NOT enable it |
| **Multi-line** patterns (X then Y on later line) | `block-absence` (brace blocks) only | **Gap**: no general "X within N lines of Y". Would need a `window:`/`near:` primitive |
| **Path/file scoping** | `target.extensions` / `filenames` / `exclude_paths` | Covered |
| **Fast pre-filter** (skip files without a literal) | `target.content_contains` | Covered |
| **File-level presence/absence** | `file-presence` / `file-absence` engines | Covered |
| **Inline suppression w/ rationale** | `keur:ignore <id>` | Covered — used for the internal-only shell calls in platform_posix/win32 |

## The concrete gaps, ranked (should we build something?)

1. **Multi-line / proximity matching** (`X within N lines of Y`) — the realistic next primitive.
   Command injection often spans lines: build a string on one line, `system()` it on another
   (exactly the SEC-005 case, which we currently catch with TWO separate patterns rather than one
   correlated match). A `near:` / `window: N` primitive would express it directly and cut false
   positives. **Recommendation: candidate for the next ADR** if multi-line rules pile up.
2. **Match-quoting in messages** (named captures → put the offending token in the finding text).
   Nice-to-have for triage UX; not correctness. **Low priority.**
3. **Lookbehind-positive** ("only if preceded by") — occasionally awkward to rewrite. Watch for it;
   if it recurs, a `require_before:` mirror of `exclude:` would close it. **Defer until needed.**

## Explicit non-goals (do NOT add — they reintroduce ReDoS)

- Backreferences, conditionals, recursion, atomic/possessive groups. These are exactly what RE2
  drops to guarantee linear time. Adding a backtracking fallback engine would hand an attacker a
  way to hang the scanner on a crafted file. If a rule truly cannot be expressed without them,
  prefer a small purpose-built engine mode (like `block-absence`) over general PCRE.

## Summary

RE2's practical limitations for KEUR rules are: **lookahead** (now solved by `exclude:`),
**multi-line correlation** (the real open gap — currently worked around with paired patterns), and
**match-quoting in messages** (cosmetic). Everything else RE2 "can't do" is either already covered
by a KEUR primitive (`skip_comments`, `block-absence`, `content_contains`, `keur:ignore`) or is a
deliberate non-goal because it would cost the linear-time guarantee. Next primitive to consider,
only if demand appears: a proximity/`window:` matcher for multi-line patterns.
