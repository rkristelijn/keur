# ADR-039 — Rule ility-alignment: every rule declares its ilities, resolution is OR/max

@id ADR-039

## Status

Proposed · 2026-10-02 · Coverage: none
· Relates to: ADR-038 (per-dimension difficulty — the vector this resolves against),
ADR-026 (rule maturity levels — the `maturity` field + the `<=` band),
ADR-011 (project profile — the global→repo cascade the vector lives in),
ADR-009 (rules are data — ilities are a data field, no engine behaviour),
ADR-014 (engine registry / lookup dispatch — the resolution is a lookup, not a branch),
`docs/design/difficulty-chooser-ui.md` (the three views that drive the vector)

## Context

ADR-038 makes difficulty a **vector**: a maturity level per dimension (an
"ility"), e.g. `security=4, quality=3, traceability=off`. ADR-026 gives each
rule a `maturity:` (1..5). What is still missing is the **binding between a rule
and the dimension(s) that steer it**. Today a rule carries a single
`category:` (a free string: `sqli`, `secrets`, `iac`, …). There are 14 distinct
category values across the corpus but no explicit, closed set of *ilities*, and
no rule says "I belong to security AND hardening".

Two real needs surface this:

1. **A rule can legitimately belong to more than one ility.** A hardcoded secret
   in a Dockerfile is both `security` and `hardening`. A SQL sink fed by an
   unvalidated request parameter is both `security` and (arguably) `reliability`.
   A single `category` cannot express that, so the difficulty vector cannot steer
   such a rule from the dimension a user actually reasons about.

2. **Which rules run must be decided at runtime from the settings.** The gate
   needs one function: given the resolved difficulty vector (global→repo) and a
   rule's declared ilities + maturity, decide active/inactive. That function must
   exist and be the *only* path the gate reads (no second code path, ADR-014).

The collaborator's framing: "a rule enforces a maturity level for e.g. both
hardening and security; which rules run is decided at runtime from the settings
mapped onto the rules — so a `maturity<=4` security rule only runs when security
is at level 4." This ADR fixes that binding and the resolution rule, and mandates
that **every** rule be aligned to at least one ility so none can silently fall
outside every slider.

## Decision

### 1. The ilities are ISO/IEC 25010 sub-characteristics, grouped by characteristic

The dimensions are the **ISO/IEC 25010 product-quality model**, taken verbatim:
the **characteristic** is the group a user expands, the **sub-characteristic** is
the ility (the slider), and keur's rule **categories are the controls** that hang
under each ility. keur fills only the characteristics for which a commit-guard
has deterministic controls; the rest (Functional Suitability, Compatibility) need
runtime/semantic judgement and belong to the analyzer (cpm), so they carry no
sliders. The set is **data** (ADR-009): a characteristic/ility appears when the
loaded corpus has controls under it.

| ISO 25010 characteristic (group) | ility (sub-characteristic / slider) | controls (rule categories) |
|----------------------------------|-------------------------------------|----------------------------|
| **Security** | confidentiality | secrets, secret, pii, data-exposure, cbi |
| | integrity | sqli, cmdi, xss, sast, supply-chain, iac, k8s, docker, container, ansible, nginx, compliance |
| | authenticity | oauth, session, api, transport, graphql |
| **Reliability** | maturity | correctness, reliability |
| | fault-tolerance | resilience |
| | availability | observability, logging |
| **Maintainability** | analysability | quality, architecture |
| | modifiability | style, patterns, antipattern, best-practice, ai-ml/slop, deprecated, migration |
| | testability | test, tests |
| | modularity | build, bundler |
| **Performance Efficiency** | time-behaviour | performance, perf, animation |
| **Usability** | accessibility | a11y, accessibility, inclusivity, forms, ui-frameworks, web |
| **Portability** | adaptability | shell-portability, platform-ifdef spread, POSIX-calls outside the abstraction layer, hardcoded path-separator (the KEUR-PORT-* rules) |
| | installability | dockerfile install-correctness |
| **(process control, ADR-010)** | traceability | docs, project, git |

Notes on placement (the "which controls belong to which ility" decisions):
- **Hardening** is not a separate ISO sub-characteristic. IaC/k8s/docker hardening
  protects the **integrity** of the deployment, so it maps to Security →
  integrity.
- **Portability** is in scope for a guard: a shell script that uses bashisms
  breaks on Alpine/BusyBox (ash) — a deterministic, pre-commit-catchable
  adaptability defect. keur's own KEUR-PORT-* rules live here (by id prefix, since
  their category is `architecture`).
- **ai-ml/slop** is maintainability debt (modifiability) with a security tint; its
  primary ility is modifiability, secondary security.
- **traceability** is a process control (ADR-010), not an ISO product-quality
  characteristic; it is shown as its own group outside the ISO tree.

The table is the full seed used to migrate both keur's own corpus and the cpm
corpus (1004 rules). After backfill each rule carries its ilities explicitly; the
table is the load-time fallback (§5) and the UI's group→ility→controls structure
(docs/design/difficulty-chooser-ui.md).

The set is **data, not code** (ADR-009): it is the union of ilities declared by
the loaded corpus. Adding a new ility to a rule adds a dimension to the chooser
with no engine change. The table above is the *seed* mapping used to backfill the
existing corpus (see §4); after backfill, each rule carries its ilities
explicitly and the table is no longer consulted at runtime.

### 2. `ilities:` on a rule — primary + secondary

A rule declares one or more ilities. The **first is primary** (where the rule
"lives" in the UI and which dimension counts it toward the average); the rest are
**secondary** (it also scales with those dimensions).

```yaml
id: KEUR-IAC-SECRET-001
maturity: 1
ilities: [security, hardening]   # primary = security, secondary = hardening
```

Single-ility rules are the common case and may be written as a scalar:

```yaml
ilities: security
```

### 3. Resolution — OR / max (the runtime mapping)

A rule is **active** when the *strongest* of its ilities' levels meets the rule's
maturity bar:

```text
active(rule, vector) :=  max( vector[i] for i in rule.ilities, where i != off )  >= rule.maturity
```

- An ility set to `off` does **not** contribute (it is dropped from the `max`).
- If **all** of a rule's ilities are `off`, the rule is inactive.
- `action` (block/warn/advisory, ADR-026 §3) decides the verdict once active —
  orthogonal, unchanged.

**Why OR/max, not AND/min.** keur is a *guard*. With OR/max, lowering a
*secondary* ility can never silently switch off a dangerous rule: a
security+hardening rule stays on as long as `security` is high enough, even if
`hardening` is turned down. AND/min would let the hardening slider mute a
security rule — the exact silent-gap failure ADR-038 forbids. For a safety gate,
the strongest claim on a rule wins.

Worked example (the collaborator's case): a rule `maturity: 4`,
`ilities: [security, hardening]`. With `security=4, hardening=2`:
`max(4, 2) = 4 >= 4` → **active** (security carries it). With `security=3,
hardening=2`: `max(3,2)=3 < 4` → inactive. So "a maturity-4 security rule only
runs when security is at 4+" holds, and the secondary hardening level cannot
disable it.

### 4. Every rule MUST be aligned; backfill + self-check

- **Mandatory field.** Every `.rule` must declare `ilities` (or legacy
  `category`, see §5). A rule with neither, or with an ility outside the closed
  set, is a **self-check finding** (dogfooding, like the existing KEUR-ARCH
  self-checks). This guarantees no rule falls outside every slider.
- **Backfill.** A one-off idempotent script derives `ilities` from each rule's
  existing `category` via the §1 table, writing multi-ility where the mapping
  says so (supply-chain→[security,hardening], iac→[hardening,security],
  architecture→[quality], data-exposure/cbi→[pii,security]). Rules missing
  `maturity` keep ADR-026's default of `3`.

### 5. Backward compatibility with `category`

`category:` stays valid and is **not** removed. When a rule has no `ilities`, the
loader maps its `category` to an ility via the §1 table at load time, so existing
rules and freshly imported cpm rules keep working unchanged. `ilities` is the
explicit, multi-valued successor; `category` remains the single-valued shorthand
and the human-facing grouping label. A rule may carry both (category for display,
ilities for steering).

### 6. Per-rule override still wins (expert tier)

The manager TUI's per-rule on/off (ADR-038 §3 expert tier, `[difficulty.rules]`)
is applied **after** resolution: an explicit `KEUR-X = on|off` overrides whatever
`active()` computed. Precedence, later wins: base level → per-dimension vector →
per-rule override. This ADR defines the middle step; the other two are unchanged.

## Why not the alternatives

- **Keep a single `category` and derive the dimension only at load time**:
  cannot express multi-ility (a secret-in-Dockerfile is one category but two
  concerns), so the vector cannot steer it from both sliders. Derivation stays as
  the backward-compat fallback (§5), not the model.
- **AND/min resolution**: lets a secondary slider mute a dangerous rule — unsafe
  for a guard (§3).
- **Free-form ility tags (open set)**: a typo'd ility would silently create a
  dead dimension no slider shows. A closed set + self-check (§4) makes a bad
  ility a visible error, not a silent gap.
- **Weighted contribution across ilities**: more expressive but non-ordinal and
  hard to reason about; `max` over a 1..5 band is simple and matches "the
  strongest concern decides".

## Consequences

**Positive**
- One runtime mapping (`active(rule, vector)`) is the single source the gate
  reads; the three views (ADR-038) all steer that one vector, so they cannot
  drift (the "one mapping, everything compatible" requirement).
- Multi-ility is first-class, so a rule steered by the dimension a user actually
  thinks about, from either concern, with the safe OR/max semantics.
- Every rule is provably aligned (self-check), so no rule is unreachable by any
  slider — the corpus stays steerable as it grows toward ~1000 cpm rules.
- No engine behaviour change: ilities are data (ADR-009); resolution is a lookup
  table keyed by ility (ADR-014).

**Negative / cost**
- A backfill pass over the corpus (272 rules now) + maintaining the closed ility
  set as new categories arrive (mitigated: the set is derived from the corpus and
  guarded by the self-check).
- The ruleset layer gains a per-ility `max` lookup instead of one global `<=`
  compare — modest, a map keyed by ility.

## Follow-ups

1. Backfill script: `category → ilities` + ADR-026 default maturity, idempotent,
   dry-run first. (scripts/)
2. Self-check rule: `.rule` without `ilities`/`category`, or with an ility
   outside the closed set → finding.
3. Resolution in the ruleset layer: `active(rule, vector)` as OR/max, `off`
   drops the ility; unit-tested on multi-ility, off, and missing-slider cases.
4. `[difficulty]` vector + `[difficulty.rules]` parsing, global→repo merge
   (ADR-011 cascade) — shared with ADR-038.
5. The three views render the resolved state (ADR-038 / difficulty-chooser-ui).
