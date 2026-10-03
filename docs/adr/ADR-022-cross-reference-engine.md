# ADR-022 — `cross-reference` engine: corpus-scoped "set A ⊆ set B" checks

@id ADR-022
**Status**: proposed *(the "direction to discuss" — see note below)* · **Date**: 2026-09-21 · **Coverage**: none *(no code yet; this records the decision before building)* · **Relates to**: ADR-001 (RE2, per-file/linear — the constraint this bends), ADR-014 (engine registry — where it would attach), ADR-020 (path-derived — the single-file sibling of this idea), ADR-021 (--trace — would observe it), and `docs/design/re2-limitations-and-rule-primitives.md` ("multi-line / cross-file correlation" gap)

> **Note on status.** keur has no separate RFC process; ADRs in `proposed` play
> that role — they record a decided *direction* with rationale, still open to
> rejection, before any code exists. This ADR is exactly that: the decision to
> add a cross-file engine, written so we can commit to (or drop) the shape
> before spending the build. It does not authorise implementation on its own.

## Context

Some invariants are **cross-file**: a value extracted from file A must also
appear in file B. The one that triggered this: **every engine registered in
`src/rules/rule/engines.cpp` must also be described in the AI primer
`eval/Modelfile`** — otherwise the `keur-rule-engineer` model never learns the
new engine and can't propose rules that use it (this drifted the moment
`path-derived` was added; it was caught by eye, not by a control).

The same shape recurs everywhere:

- every route registered in code has a matching entry in the API docs;
- every feature flag in code exists in the config schema;
- every `@id` concept has an `@implements` (today done by a bespoke shell script,
  `doc-coverage.sh`, not the engine);
- every public export is re-exported from the package index.

**No current engine can express this.** Every engine — `pattern`, `presence`,
`absence`, `count`, `multiline`, even `path-derived` (ADR-020) — runs over **one
file at a time**: the `ScanContext` gives `cx.lines` (this file) and `cx.rel`
(this path), nothing else. `path-derived` derives its expectation from the path
but still checks *within the same file*. "Is token X from file A also present in
file B" needs to see two files (or the whole set) at once. That is the deliberate
boundary of ADR-001 (per-file, linear, no corpus state) — bending it is the
whole point of this ADR.

There is a foothold: the **file-level engines** (`file-absence` /
`file-presence`) already run **once over the entire fileset** in `scan.cpp`'s
`eval_file_level_rules`, outside the per-line registry. A corpus-scoped engine
belongs in that same "once over everything" phase.

## Decision (proposed)

Add a **`cross-reference`** engine that runs once over the whole corpus and
asserts a **subset relation** between two token sets extracted by regex:

- **source** — files matched by `source_glob`, tokens captured by
  `source_capture` (group 1). E.g. `engines.cpp` → `{"([a-z-]+)",\s*&eng_` →
  `{pattern, presence, …}`.
- **target** — files matched by `target_glob`, tokens captured by
  `target_capture` (group 1). E.g. `Modelfile` → `^- ([a-z-]+)\s*:` →
  `{pattern, presence, …}`.
- **relation** — `subset` (default): flag each source token **absent** from the
  target set. (`equal` — flag either-way differences — is a possible variant.)

The finding names the missing token and points at the source file/line.

### What it needs (the cost, stated plainly)

1. **A `CorpusContext`** (sibling of `ScanContext`) giving an engine the full
   `[{rel, content}]` set — the fileset access engines deliberately lack today.
2. **A corpus-engine phase** in `scan.cpp` alongside `eval_file_level_rules`
   (once over everything, not per file). The per-line registry (ADR-014) stays
   untouched; this is a second, small registry or a branch in the file-level
   pass.
3. **New `Rule` fields**: `source_glob`, `source_capture`, `target_glob`,
   `target_capture`, `relation`. Parser reads them (field table in `parse.cpp`).
4. **A `@proves` test** + a `docs/rules-authoring.md` entry (AGENTS.md rule for a
   new engine). RE2-safe: two ordinary captures, no backrefs; the O(files) pass
   runs once, so the linear-time guarantee holds at the corpus level.

The first rule would be **KEUR-META-001**: engines in `engines.cpp` ⊆ engines in
`eval/Modelfile` — the drift that motivated this, now guarded by keur itself.

## Why not the alternatives

- **`path-derived` (ADR-020)**: single-file; derives from the path but cannot
  read a second file. Wrong tool for a cross-file relation.
- **Keep it in `doc-coverage.sh`** (a shell script, as with `@id`→`@implements`):
  works and is cheap, but it is a bespoke check outside the engine, invisible to
  `keur scan`, not reusable as data, and not part of "keur guards itself with its
  own engine". Fine as a stopgap; not the principled home.
- **A general graph/facts model over the codebase**: far larger surface;
  overkill for a subset check. Deferred (a future ADR if graph queries pile up).
- **Per-file only, accept the gap**: leaves every cross-file invariant
  un-enforceable — the recurring need above says that is too limiting.

## Consequences

- keur gains a reusable primitive for the whole "A must be mirrored in B"
  family (code↔docs, code↔primer, code↔config-schema, export↔index), each then a
  `.rule`, not a script — the same "engine dumb, rule is data" line as ADR-009.
- The engine model grows a *corpus* scope beside the per-file scope. That is a
  real widening of ADR-001's boundary, done once, in one place, with the fileset
  passed explicitly — business engines still hold no I/O.
- The `engine → Modelfile` drift becomes self-guarding (KEUR-META-001).
- Until this is built, the Modelfile drift is covered cheaply by a line in
  `doc-coverage.sh` (engine set ⊆ primer set), which is the interim control.
- Boundary held where it counts: still RE2, still linear (one O(files) pass),
  still one binary, still I/O only at the injected edge.

## Open questions (to settle before building)

1. **Relations**: ship only `subset`, or also `equal` / `superset`?
2. **Capture power**: one capture group enough, or allow `\1..\n` like
   path-derived (e.g. compose a target token from two source captures)?
3. **Scope of globs**: repo-relative globs vs. keur's existing
   `target.extensions/filenames` machinery — reuse or new?
4. **Performance ceiling**: cap corpus size / file size for the second pass, or
   rely on the existing 1 MB per-file limit + directory prune in `walk`?
