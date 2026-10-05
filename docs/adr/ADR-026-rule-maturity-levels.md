# ADR-026 — Rule maturity levels & profile templates

**Status**: proposed · **Date**: 2026-09-23 · **Coverage**: none · **Relates to**: ADR-011 (project profile: owner/lifecycle/criticality/maturity), ADR-009 (rules are data), ADR-018 (cpm as a rule source)

## Context

keur can now load essentially all of cpm's rule corpus (≈1000 rules; only the
`extract-duplicates` engine gap remained, now closed). But **shipping every rule
as an active gate is far too strict** — a throwaway prototype would be buried in
findings that only a mature, mission-critical service should be held to. That is
exactly the tension ADR-011 named: "a prototype and a mission-critical service
run the same hook but should be held to different bars."

Today a rule's `action` (block/warn/advisory) and `severity` are fixed per rule.
There is no notion of *which rules apply at which maturity*, so the only way to
tune strictness is enabling/disabling rules by hand (the manager TUI) — which
does not scale to 1000 rules.

## Decision

Introduce a **maturity level** on every rule and a small set of **profile
templates** that select which levels are active.

### 1. `maturity:` field on a rule (data, ADR-009)

Each rule declares the *lowest project maturity at which it should gate*. A
5-level scale, aligned with ADR-011's CMMI-flavoured `maturity` axis:

| level | name | meaning — "enforce this from here up" |
|-------|------|----------------------------------------|
| `1` | prototype | only defects that break the build or leak secrets (conflict markers, committed keys, `.env`) |
| `2` | developing | + correctness basics (SAST criticals, obvious injection) |
| `3` | defined | + maintainability (file/dir size, nesting, dead code, duplication) |
| `4` | managed | + coverage/traceability gates, style consistency |
| `5` | optimising | + advisory nudges, opinionated framework/idiom rules |

Absent `maturity:` defaults to `3` (defined) — the sensible middle. The field is
metadata the policy layer reads; the engine ignores it (rules stay data).

### 2. Profile templates select the active band

A project's profile (ADR-011) names a maturity; keur activates every rule whose
`maturity <=` the project's level. Ship a handful of ready-made templates so a
team does not hand-pick from 1000 rules:

- `prototype.toml` — maturity 1 (secrets + build-breakers only)
- `startup.toml` — maturity 2
- `standard.toml`    — maturity 3 (default)
- `hardened.toml`    — maturity 4
- `mission-critical.toml` — maturity 5

Templates live under `config/profiles/`. A repo picks one in its `keur.toml`
(`profile = "standard"`), or sets `maturity = N` directly. Repo overrides global
(the ADR-011 cascade).

### 3. Interaction with `action`

`maturity` decides IF a rule is active; `action` (block/warn/advisory) decides
its verdict once active. A rule can therefore be "active from maturity 3, warns"
vs "active from maturity 1, blocks". This keeps the two concerns orthogonal.

## Consequences

- Importing the full cpm corpus becomes safe: low-maturity projects see only the
  deadly/breakage rules; strictness scales with the profile, not by hand.
- One new data field + a template loader + a filter in the policy/ruleset layer.
  No engine change (ADR-009 holds — rules stay data).
- Requires backfilling `maturity:` across the corpus. Start with defaults (3),
  then lower the deadly rules (secrets, conflict markers, committed keys) to 1
  and raise the opinionated/framework rules to 5. This can be incremental.
- The manager TUI (ADR-manager) should show maturity per rule and let a scope
  pick a profile, not just toggle individual rules.

## Alternatives considered

- **Per-rule enable/disable only (status quo):** does not scale to 1000 rules.
- **Encode maturity in `action`:** conflates "is it active" with "how hard does
  it fail" — a rule cannot then be "active-but-warn at low maturity".
- **Tags/categories instead of a level:** less ordinal; a `<=` band is simpler to
  reason about and to template than an arbitrary tag set.

## Open questions

- Exact maturity assignment per imported rule family (a bulk pass, likely
  seeded from cpm's own severity + category).
- Whether `criticality` (ADR-011) should *raise* the effective maturity (a
  tier-1 service demands a higher bar even while "developing").
