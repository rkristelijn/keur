# ADR-015 — Readability & naming controls (parked)

@id ADR-015
**Status**: proposed (parked) · **Date**: 2026-09-19 · **Coverage**: none · **Relates to**: ADR-014 (uses the pattern/count engines — no new engine needed), ADR-013 (the semantic parts land in its out-of-scope/parked list), ADR-004 (the RE2/human-judgement boundary)

<!-- Coverage: none — decision recorded, not yet built. We are on the engine
     refactor; this is the readability/naming direction, parked for later. -->

## Context

Names and clarity are the most essential, least tool-checked part of code. Three
readability ideas surfaced, all real and industry-named — not invented:

1. **Cryptic conditions → a named boolean.** `if (age >= 18 && !banned &&
   verified)` → `const bool isEligible = ...; if (isEligible)`. This is Fowler's
   *extract condition / introduce explaining variable*. It reads better, and —
   the key benefit — the condition becomes **testable in isolation** without
   entering the control flow. When it is business logic, the named condition can
   even live centrally (a "rules of the domain" home).
2. **Naming conventions.** Positive booleans (`isValid`, never `isNotInvalid` —
   double negation kills readability); boolean prefixes (`is/has/can/should`);
   query-ish names for data (`get/fetch/find`).
3. **Command-Query Separation (CQS, Bertrand Meyer).** A function is either a
   **query** (returns a value, no side effects) or a **command** (side effect,
   returns nothing) — never both. `getUser()` that also writes/logs violates CQS.
   (This is CQS, not ports-and-adapters/hexagonal — a different pattern.)

The motivation is not style-policing for its own sake: clearer names and
extracted conditions are **more testable** (test the condition, not the flow) and
make the business logic's heart explicit and centralisable.

## Decision (direction — parked, not yet built)

Add readability/naming controls, but split hard by **what RE2 can enforce** vs.
**what needs semantics** — so we never ship noisy rules the gate itself should
reject.

### Enforceable now (surface patterns — pattern/count engines, no new engine)

| Control | Rule shape | Verdict |
|---------|-----------|---------|
| **Negative boolean names** | `pattern`: `\b(is|has|can|should)Not[A-Z]` / `\b[A-Z](isNo|hasNo)` | WARN — "name the positive: `isEligible`, not `isNotIneligible`" |
| **Cryptic condition** | `count`/`pattern`: an `if`/`while` head with ≥3 boolean operators (`&&`/`||`) | WARN — "extract to a named condition; it reads better and is testable in isolation" |
| **Query/command name mixing (CQS proxy)** | `pattern`: a `get/fetch/find/is/has`-named function whose name also carries a mutating verb (`save/write/send/delete/update`) | WARN — "a query name should not imply a command (CQS)" |

These are **naming and surface-complexity signals** — one-line, high-signal, the
80% of readability wins. They use the existing pattern/count engines.

### Parked as out of scope (needs semantics — see ADR-013)

- **True CQS enforcement** — proving a *query actually has a side effect* requires
  dataflow/effect analysis, which RE2 cannot see. keur can flag the *name* proxy,
  never the real effect. This belongs in ADR-013's "needs human judgement /
  deeper analysis" list.
- **"Is this identifier actually a boolean?"** in untyped code — undecidable by
  regex; would be noisy. Only the explicit-prefix cases are enforceable.

## Why not

- **Enforce full CQS / real side-effect detection**: crosses the RE2 boundary
  (ADR-004) into semantic analysis; would be slow and false-positive-heavy.
  Flag the name proxy instead; park the rest.
- **Block on style**: readability is a nudge, not a correctness error — these are
  WARN, not BLOCK. Consistent with the acceptable-gaps tiering (ADR-010).
- **A dedicated engine**: not needed — pattern + count already express all the
  enforceable controls.

## Consequences

- When picked up: three low-noise WARN rules (negative-boolean, cryptic-condition,
  CQS-name-proxy) built like `KEUR-QUAL-001`, test-first, no engine change.
- The semantic CQS check stays honestly parked (ADR-013), keeping the bright line
  between name/surface enforcement and effect analysis.
- Readability becomes measurable where it is cheap (names, condition complexity)
  and explicitly deferred where it is not (real effects) — the keur pattern:
  simple, proven, honest about the limit.
