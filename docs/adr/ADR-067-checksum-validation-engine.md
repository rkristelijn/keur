# ADR-067 — Checksum-validation engine: the arithmetic a regex can't do, driven by a rule

@id ADR-067

**Status**: Proposed · **Date**: 2026-10-25 · **Coverage**: none
· Relates to: ADR-009 (rules are data, the engine is dumb — new detection is a
`.rule`, not engine logic), ADR-014 (engines are a compile-time strategy
registry — one entry per engine), ADR-049 + `@id engine-base64-unwrap` (the
precedent: a C++ transform a regex cannot do, with detection still in the rule's
patterns), ADR-042 / ADR-046 (PII vault + allowlist — never echo the matched
value), ADR-002 (single binary — no shell adapter, no external tool for the
default gate), ADR-066 (external scanners are opt-in; this moves a class of
detection INTO the engine so it needs no external tool)

## Context

Some of the highest-value PII checks are **algorithmic, not textual**: a Dutch
BSN is validated by the *11-proef* (a weighted digit sum mod 11), an IBAN by
*mod-97*, a credit-card number by the *Luhn* checksum. A regex can match the
*shape* (9 digits, an IBAN-like string) but **cannot compute a checksum** — so a
pattern rule alone either misses real values or drowns in false positives (every
9-digit number looks like a BSN).

Until this PR this capability lived in `scanners/pii.sh` — a shell **adapter**
over an external `pii-sensitive-scan.sh` that did the 11-proef / mod-97
arithmetic. That was three things we have been retiring: a shell shim (ADR-002),
an external dependency for a core check (ADR-066), and logic that lived outside
the engine where `keur check` could not reach it without spawning a process. The
adapter was removed with this ADR; the *capability* it fronted (checksum-
validated PII) is real and not yet expressible as a keur rule.

The user's question is the right design: **put the check in C++ and drive it
from a rule.** keur already does exactly this for base64 (`engine-base64-unwrap`,
ADR-049): the engine performs the transform a regex cannot (decode), while the
*detection* stays in the rule's `patterns[]` (data). A checksum is the same
shape — arithmetic in C++, configuration in the `.rule`.

## Decision

Add a **`checksum` validation engine** to the strategy registry
(`rule/engines.cpp`, ADR-014) — one small, pure C++ function that validates a
candidate string against a named algorithm. Detection stays in the rule (ADR-009):

- the rule's `patterns[]` finds *candidates* by shape (e.g. `\b\d{9}\b` for a
  BSN, an IBAN-shaped token), exactly as any pattern rule does;
- a new rule field selects the validator, e.g. `validate: elfproef` /
  `validate: mod97` / `validate: luhn`;
- the engine runs the pattern, then **keeps only the candidates that pass the
  checksum**. A match is a finding only if the shape matches *and* the arithmetic
  confirms it. This is what collapses the false-positive problem a pure regex has.

The validators are the closed, well-specified set that covers the retired
scanner: `elfproef` (BSN, weighted mod-11), `mod97` (IBAN / many national ids),
`luhn` (card numbers / IMEI). Each is a few lines of pure arithmetic, unit-tested
in isolation (`@proves ADR-067`), with no I/O — it fits the "small single-purpose
function" rule (AGENTS.md) and the dumb-engine principle: the engine computes,
the rule decides what to look for and what verdict to assign.

As with base64-unwrap and the PII vault (ADR-042/046), a finding points at the
**candidate's location, never its value** — keur does not echo a BSN or a card
number into its own output.

### What this is NOT

- **Not reimplementing an external PII suite.** The scope is the checksum
  *validators* a regex cannot express, not a full PII product. Richer PII
  (NER, context) stays the opt-in external lane (ADR-066) and the `$PII_VAULT`
  design (ADR-042).
- **Not a new brain.** A validator is fixed arithmetic with one answer
  (valid / not). Same input, same verdict — the determinism contract holds.
- **Not engine-per-algorithm sprawl.** One `checksum` engine with a named
  `validate:` selector, not a separate engine type per checksum (ADR-014: data
  branches → one engine + a lookup, not N engines).

## Consequences

- **Positive**: BSN / IBAN / card detection becomes real keur **rules** on a
  compiled engine — deterministic, sub-second, zero external tools, available in
  `keur check` out of the box. The exact capability the dead `scanners/pii.sh`
  fronted, now inside the binary (ADR-002/066).
- **Positive**: kills the false-positive problem of shape-only PII regex (every
  9-digit number is no longer a "BSN") — the checksum is the precision filter.
- **Positive**: retiring `scanners/pii.sh` + `scanners/rules.sh` loses nothing —
  `rules.sh` was a pure wrapper over `bin/keur-rules`, and `pii.sh`'s capability
  is captured here as the engine's successor design.
- **Cost**: one engine entry + a small validator table + `@proves` unit tests +
  a `docs/rules-authoring.md` note (ADR-014's "new engine" checklist). Bounded.
- **Risk (named)**: a validator must not echo the matched value in its message
  (ADR-042/046); the `@proves` test asserts the finding carries a location, not
  the digits.

## Follow-ups

1. Implement `engine-checksum` in `rule/engines.cpp` with `elfproef` / `mod97` /
   `luhn` validators (pure, unit-tested); wire the `validate:` field in the parser.
2. Author `rules/pii/` rules: BSN (`\d{9}` + `validate: elfproef`), IBAN
   (shape + `validate: mod97`), card (`validate: luhn`). Fixtures with valid +
   invalid-checksum cases; a `@proves` test asserts the invalid ones do NOT fire.
3. Document the engine in `docs/rules-authoring.md`.
4. ~~Remove `scanners/` (dead shims)~~ — done in this PR; capability preserved here.
