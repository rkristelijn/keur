# ADR-079 — A block rule must be able to block

@id ADR-079
**Status**: accepted · **Date**: 2026-11-24 · **Coverage**: code · **Relates to**: ADR-026 (rule maturity), ADR-035 (self-explaining findings)

## Context

The policy blocks a finding when its action is `block` **and** its confidence is
at least `block-at` (default 0.85). A rule declares both independently: `action`
(or a severity that derives it) and `confidence`. Nothing checked that the two fit.

A field test on eslint-plugin-mui (2026-11-24) planted
`const AWS_SECRET_ACCESS_KEY = '...'`. KEUR-SEC-007 found it, printed
`warn KEUR-SEC-007 | block | conf 0.8`, and let the commit through. The output
said "block" while the gate could never block on it. That is the worst kind of
finding: it looks like a guard and is not one.

13 shipped rules were in this state:

| Rule | Confidence |
|---|---|
| KEUR-INCL-001 | 0.6 |
| KEUR-NODE-010 | 0.65 |
| KEUR-NEST-010, KEUR-SESS-001 | 0.7 |
| KEUR-CICD-002, KEUR-SEC-008, KEUR-SESS-002 | 0.75 |
| KEUR-NEST-001, KEUR-NOSQLI-001, KEUR-SEC-007, KEUR-SQLI-001, KEUR-SQLI-002, SECRETS-090 | 0.8 |

## Decision

**Invariant:** a shipped rule whose effective action is `block` has a confidence
of at least the default `block-at`. An integration scenario
(`@proves ADR-079`) enforces it over the whole rule set.

Per rule, data decides (measured 2026-11-24 over 20 repos, committed source only):

- **KEUR-SEC-007 now blocks** (confidence 0.9). There were zero hits in committed
  source; the only hits were in gitignored build output. It is the one rule that
  catches `*_SECRET_*_KEY = '...'` assignments, so warning on a leaked secret is
  the wrong default. Test, fixture and `*.example` paths are excluded so
  placeholder credentials do not block.
- **The other 12 become `action: warn`.** This is behaviour-neutral at the
  default policy: they already only warned. Now the output says so. The SAST
  rules only fired in deliberately vulnerable repos (mestjs, apex-cli
  vuln_target); KEUR-INCL-001 and SECRETS-090 are context-dependent by design.
  Promoting any of them to block needs its own precision evidence, not a
  confidence bump.

## Consequences

- A new rule cannot ship with `block` and a low confidence. The scenario names
  the offender, and the fix is a one-line choice.
- A repo that lowers `block-at` below 0.85 no longer gets these 12 rules as
  blockers. That was never documented behaviour; set `action` per rule in the
  repo cascade if you want it.
