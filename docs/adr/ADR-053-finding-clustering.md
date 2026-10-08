# ADR-053 — Finding clustering: when several rules report the same issue

@id ADR-053
**Status**: proposed · **Date**: 2026-10-08 · **Coverage**: none (report-layer clustering to be designed) · **Relates to**: ADR-010 (traceability — every finding keeps its rule id), ADR-035 (self-explaining findings & suppression), ADR-052 (rule-id de-dup, the easy half of this), ADR-009 (rules are data; the engine stays minimal)

## Context

keur emits one finding per matching rule. Sometimes several *different* rules
describe the **same underlying issue** from different angles, so one real problem
shows up as a cluster of near-duplicate warnings. Observed on a fresh repo
(2026-10-08):

```
warn  KEUR-QUAL-002  No SECURITY.md — add one so people can report vulns.
warn  PROJ-003       Missing SECURITY.md
warn  PROJ-001       Missing README.md
warn  PROJ-002       Missing CONTRIBUTING.md
warn  PROJ-004       Missing LICENSE file
```

`KEUR-QUAL-002` and `PROJ-003` are the *same* finding ("no SECURITY.md") from two
rule families. The `PROJ-00x` set is a related family ("project hygiene files").
This is distinct from ADR-052's fix: that removed a rule firing *twice* (same id,
two dirs). This ADR is about *different ids* that overlap in meaning.

The noise matters: a wall of near-duplicate warnings trains the reader to skim,
and skimming is how a real finding gets missed. But naive de-duplication is
dangerous — collapsing `KEUR-QUAL-002` into `PROJ-003` loses a rule id that
something downstream (traceability ADR-010, a `[rules] disabled` entry, a
`keur:ignore`) may reference.

## Problem statement (what a decision must answer)

1. **What counts as "the same finding"?** Same file+line is too narrow
   (file-level findings sit at line 0) and too broad (two real issues can share a
   line). Options: an explicit `supersedes:`/`aliases:` field on a rule; a shared
   "concept" tag; or same (file, normalised-message-key).
2. **Which rule wins, and do we keep the others?** Traceability (ADR-010) says an
   id must not silently vanish. Leaning: keep ALL ids in the finding, present ONE
   primary line with the rest as "(also: PROJ-003)".
3. **Where does clustering happen?** It must NOT change the policy verdict
   (BLOCK/WARN/PASS is a pure function of findings — ADR-002/035). So clustering
   belongs in the **report layer** (presentation), after the policy has already
   decided, OR it keeps every finding for the verdict and only collapses the
   *rendered* output. Leaning: report-layer only; the verdict still sees all
   findings.
4. **Opt-in or automatic?** A wrong cluster hides a finding. Leaning: explicit
   declaration (a rule names what it supersedes/aliases) over heuristic
   message-similarity, so clustering is auditable and never a guess.

## Options

- **A — Rule-declared `supersedes:` / `cluster:` field (data, ADR-009).** A rule
  declares the ids it subsumes or the concept it shares. The report groups by
  that. Deterministic, auditable, no heuristic. Cost: authors must declare it;
  unmanaged overlaps stay un-clustered (acceptable — fail open, show both).
- **B — Heuristic clustering by (file, message-similarity).** Zero authoring, but
  a similarity threshold is a guess; a wrong merge hides a finding. Rejected as
  the primary mechanism; maybe a *reporting hint* ("3 similar findings") that
  never actually drops one.
- **C — Do nothing; rely on `[rules] disabled`.** The reader silences the
  overlap per repo. Already possible, but it is manual per repo and per id, and
  it removes the finding for everyone, not just de-noises the view.

Leaning: **A**, with **B** allowed only as a non-destructive "N similar" hint.

## Decision

Deferred — this ADR frames the decision; it is not yet made. The concrete
mechanism (the `supersedes:`/`cluster:` field shape, the report-layer grouping,
whether a "concept" taxonomy is worth it) needs a short spike. What IS decided:

- Clustering, when built, lives in the **report layer** and must not change the
  policy verdict (ADR-002/035).
- It must be **declaration-driven** (data on the rule, ADR-009), never a silent
  similarity heuristic that can drop a finding.
- Every clustered finding **keeps its rule id** in the output (ADR-010); a
  cluster shows a primary + the subsumed ids, it does not erase them.

## Consequences

**If built as leaning (A, report-layer)**
- Less noise: one real issue reads as one line, with the overlapping ids visible
  but not shouting.
- Verdict unchanged; suppression and traceability by id still work.
- Authoring cost: overlapping rule families (`PROJ-00x`, `KEUR-QUAL-002`) must
  declare their relationship once.

**Risks**
- A clustered-away id that someone references in a `keur:ignore` must still match
  — so suppression runs on the full finding set, before rendering, not after.
- Over-clustering hides issues; keep it opt-in and declaration-driven, fail open.

## Open questions

- Is a first-class "concept" taxonomy worth it, or is pairwise `supersedes:`
  enough for the handful of real overlaps?
- Should the `PROJ-00x` "project hygiene" family be a single composite rule
  (ADR-009) instead of N rules, so the overlap never arises? That may dissolve
  part of this problem without a clustering layer at all.
- Does the manager/report need a "grouped" vs "flat" view toggle, or is grouped
  always better?
