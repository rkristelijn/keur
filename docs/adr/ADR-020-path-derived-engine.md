# ADR-020 — `path-derived` engine: checks whose expectation comes from the file path

@id ADR-020
**Status**: proposed · **Date**: 2026-09-21 · **Coverage**: code · **Relates to**: ADR-001 (RE2 line engine), ADR-003 (layer↔namespace), ADR-014 (engine strategy-registry — this engine registers on it), ADR-009 (facts/composite, the other "engine is dumb, rule is data" primitives)

## Context

keur enforces "each layer directory maps 1:1 onto a C++ namespace" (ADR-003)
with **four near-identical rules** — KEUR-ARCH-002/003/004 (adapters/cmd/report)
— each an `absence` rule looking for one *literal* string (`namespace
keur::adapters`, …) and *excluding every other directory* so it only scans its
own layer. Two costs:

1. **Duplication that doesn't scale.** A new layer (`config/`, `manager/`, …)
   needs a *new* rule **and** an edit to every other rule's `exclude_paths` (so
   they don't fire on the newcomer). We hit exactly this adding `config/`.
2. **The invariant is relational, and no engine can express it.** The real rule
   is "a file in `src/keur/<layer>/` must declare `namespace keur::<layer>`" —
   the expected string is a *function of the file's path*. Every existing engine
   matches a **fixed** string; RE2 has no backreference to the path. So the
   relation can only be faked by one-literal-per-layer rules.

This is not a keur-only wart. It is a well-known lint category:

- **`eslint-plugin-check-file`** — `filename-naming-convention`,
  `folder-naming-convention`: the *filename* must match a pattern derived from
  its folder (e.g. files in `components/` are `PascalCase`).
- **`eslint-plugin-project-structure`** — folder structure as a tree; "if
  `Component.tsx` exists, `Component.test.tsx` must too" (expectation derived
  from a sibling's name).
- **`eslint-plugin-filenames`** (`match-exported`) — the filename must equal the
  name of the thing the module exports.
- **`eslint-plugin-boundaries`** / import-boundary plugins — what a file may
  import is derived from *which layer directory it lives in*.
- **Java/Kotlin/Go** compilers/linters — the `package` declaration must match
  the directory path. Same shape as our namespace rule.
- **cpm's 4-element naming formula** (`domain-flavor-intent-method`,
  `docs/conventions.md`) — a name/path convention one would want to enforce.

All of these are "**the expectation is computed from the path/name, not a
constant**". keur has no primitive for it. That is the gap.

## Decision

Add one engine, **`path-derived`**, that registers on the ADR-014 strategy
registry (one entry, scan loop unchanged). It:

1. applies a **`path_capture`** regex to the scanned file's relative path;
2. if it matches, substitutes capture group `\1` into a **`derived_template`**
   to build the *expected* string for this specific file;
3. requires that expected string to be **present** in the file — if it is
   absent, emit the finding. (Present-or-flag, like `absence`, but the sought
   string is derived per file instead of fixed.)

Two new `Rule` fields carry the data (`rule_engine.h`), two new parser keys read
them (`parse.cpp`'s field table), one new engine function is registered
(`engines.cpp`). No new `ScanContext` — engines already receive `cx.rel` (the
path). RE2-safe: `path_capture` is an ordinary linear regex with a capture; no
backreferences, no backtracking, so ADR-001's linear-time guarantee holds.

```text
engine: path-derived
path_capture: 'src/keur/([a-z_]+)/'      # group 1 = the layer segment
derived_template: 'namespace keur::\1'   # expected string for THIS file
patterns:
  - message: "File under src/keur/<layer>/ must declare 'namespace keur::<layer>' (ADR-003)."
```

A file at `src/keur/config/scopes.cpp` derives the expectation `namespace
keur::config`; `src/keur/manager/view.h` derives `namespace keur::manager` — no
rule edit, ever, for a new conforming layer. The four ARCH rules collapse to one
**`KEUR-ARCH-002-layer-namespace.rule`**.

`exclude_paths` still lists the *deliberate exceptions* — directories that do
**not** follow `keur::<dir>` (`common/` is `keur::platform`; `util/`, `render/`,
`learn/` have their own conventions). That is a real allowlist of exceptions,
not duplication: the positive rule is now fully dynamic; only the handful of
genuine deviations are named, in one place.

## Semantics (precise)

- If `path_capture` does **not** match `cx.rel`: the rule does not apply to this
  file (no finding). This is how the engine scopes itself — no positive
  path-include needed.
- If it matches: build `expected` by replacing `\1` (and `\2`, … if present)
  in `derived_template` with the captured groups. Substitution is literal text,
  not regex, so the derived string is matched as a fixed substring (via a
  quoted RE2 or a plain `find`), keeping it ReDoS-free.
- If `expected` appears on any (non-suppressed) line: pass. Else: emit the
  finding at line 1 (same as `absence`). Inline `keur:ignore` still works.

## Why not the alternatives

- **Keep four literal rules + edit excludes per layer** (status quo): the
  duplication and per-layer edits we are removing.
- **One rule with a path→string table baked in**: moves the list into one file
  but still needs an edit per new layer; the mapping is still hardcoded, just
  relocated. `path-derived` removes the mapping entirely.
- **A runtime/scripted check** outside the engine: breaks "keur guards itself
  with its own engine" and the single-binary model (ADR-002).
- **Generalise into arbitrary path→path rules** (does sibling file exist, etc.):
  useful (eslint-plugin-project-structure does it) but a bigger surface. Start
  with the present-derived-string case that unblocks the namespace rule; a
  future ADR can add a `path-requires-sibling` engine if demand appears.

## Consequences

- The namespace invariant becomes **self-scaling**: add a layer, it is checked,
  zero rule edits. The thing that stops layout/namespace drift no longer drifts
  itself.
- keur gains a reusable primitive for the whole "name/path must match a derived
  value" lint family (filename↔export, package↔dir, filename-case↔folder). Each
  is then a `.rule`, not engine code — same "engine dumb, rule is data" line as
  ADR-009.
- One engine added on the ADR-014 registry; scan loop unchanged. Requires a
  `@proves` test and a `docs/rules-authoring.md` entry (per AGENTS.md "if you
  added an engine").
- Boundary held: still RE2, still linear-time, still one compiled binary.
