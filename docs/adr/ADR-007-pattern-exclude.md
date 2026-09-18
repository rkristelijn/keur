# ADR-007 — Pattern-level `exclude` (RE2-friendly "pattern-not")

**Status**: accepted · **Date**: 2026-09-18 · **Relates to**: ADR-001 (native rule engine)

## Context

KEUR's rule engine is RE2-powered (ADR-001). RE2 deliberately does **not** support
lookahead/lookbehind or backreferences, because that is what guarantees **linear-time**
matching and immunity to catastrophic backtracking (ReDoS) — a property we want in a scanner
that runs over untrusted third-party code.

That constraint bit us while writing `KEUR-SEC-005` (C/C++ shell-exec command injection). The
natural expression is *"match `system(` **except** when the argument is a string literal"*, which
in PCRE is a negative lookahead `system\(\s*(?!")`. RE2 rejects it: `invalid perl operator: (?!`.

We do **not** want to swap engines to `std::regex`/PCRE — that would trade away the linear-time /
ReDoS guarantee (and `std::regex` is slow). The requirement is narrow: express "match X except
when Y on the same line" without leaving RE2.

## Decision

Add an optional **`exclude`** field to a rule pattern (a second RE2 regex, a.k.a. *pattern-not* /
allowlist). Semantics:

> For the `pattern` engine, when a line matches `regex`, it is reported **unless** the pattern's
> `exclude` regex also matches that same line, in which case the match is suppressed.

```yaml
patterns:
  - regex:   \b(?:std::)?(?:system|popen)\s*\(
    exclude: \b(?:std::)?(?:system|popen)\s*\(\s*"      # allow a bare string literal
    message: "system()/popen() with a non-literal command — command injection (CWE-78)."
```

This is the same construct mature scanners use (semgrep `pattern` + `pattern-not`, gitleaks
`allowlist.regexes`): match broad, subtract the safe case. Two linear RE2 passes = still linear,
still one engine, still ReDoS-immune.

## Why not the alternatives

- **PCRE/`std::regex` engine**: reintroduces ReDoS risk and is slower. Rejected.
- **A second opt-in `engine: pcre`**: real maintenance surface + opens the ReDoS door per-rule for
  whoever uses it. Not worth it for the narrow need.
- **Per-case positive rewrite** (e.g. "match an identifier char instead of not-a-quote"): works
  sometimes, but not general and often less readable. `exclude` is the reusable primitive.

## Scope & limits

- Applies to the **`pattern`** engine (line-oriented). `presence`/`absence`/`block-absence` are
  unchanged for now (their semantics don't need it yet).
- `exclude` matches on the **same line** as `regex` (line-scoped, like the rest of the engine).
  Cross-line exclusion is out of scope.
- An invalid `exclude` regex is reported (like an invalid `regex`) and treated as "no exclude" —
  it never silently changes matching.
- Backwards compatible: patterns without `exclude` behave exactly as before.

## Implementation

- `RulePattern.exclude_str` (rule_engine.h), parsed from `    exclude:` (rule/parse.cpp).
- `Compiled.exclude_res` compiled parallel to `res` (rule/internal.h, rule/scan.cpp).
- In the `pattern` branch: `if (excl && RE2::PartialMatch(line, *excl)) continue;` (rule/scan.cpp).

## Consequences

- Rules can now say "match X except Y" without lookahead. First user: `KEUR-SEC-005`.
- Keeps the engine single (RE2) and linear — the ReDoS guarantee is preserved.
- Future rules that previously needed lookahead (see the "RE2 limitations" note in
  `docs/design/re2-limitations-and-rule-primitives.md`) can often be expressed with `exclude`.
