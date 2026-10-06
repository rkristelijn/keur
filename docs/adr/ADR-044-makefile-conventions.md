# ADR-044 — Makefile conventions: thin orchestration, maturity-gated, enforceable as rules

@id ADR-044

## Status

Proposed · 2026-10-06 · Coverage: none
· Relates to: ADR-002 (single binary, no build-time complexity — why not CMake),
ADR-008 (incremental compile — the `-MMD`/`-include` dependency tracking a
Makefile should use), ADR-009 (rules are data — any Makefile rule is a `.rule`,
not engine logic), ADR-026 (rule maturity levels — which conventions gate at
which maturity), ADR-035 (self-explaining findings — a Makefile finding must say
the fix), `src/keur/Makefile` (the build this standardises),
cpm `checks/universal/check-makefile.sh` (the prior art: >5 inline shell lines →
extract), apex-cli `docs/maturity-patterns.md` (the learn→guide→guard→enforce
progression)

## Context

keur gates code and briefs developers at the maturity THEY choose (ADR-026). A
Makefile, when a repo has one, is part of that code — so keur should have an
opinion on it, scaled to maturity, like any other rule.

Two things prompted this:

1. keur's own `src/keur/Makefile` had ~30 hand-written per-object compile rules,
   each repeating header prerequisites that `-MMD -MP` (ADR-008) already derive.
   Adding one source needed a second edit or the build broke — the drift smell
   that motivates a convention + control.
2. The question "should make hold this much logic, or be a thin wrapper?" The
   answer already exists in the house codebase and in the literature; this ADR
   writes it down so it is enforceable, not folklore.

What the house already knows (prior art, not invented here):

- **cpm `check-makefile.sh`**: a target with >5 physical shell lines (excluding
  `@echo` and `$(MAKE)`) should be extracted to a script. "make orchestrates;
  scripts implement."
- **The other C++ repos** (apex-cli, cpm, helmet, sysinfo) all build with
  `SRCS := $(wildcard …)` → `OBJS := $(patsubst …)` → ONE `%.o: %.cpp` pattern
  rule, with the build dir mirroring the source tree. No per-source rule, no
  flattened object names.
- **apex-cli maturity-patterns**: progressive enforcement
  `learn → guide → guard → enforce` — permissive first, stricter as the project
  matures. This is keur's maturity axis (ADR-026) in verbs.
- **Rickard, "The Unreasonable Effectiveness of Makefiles"**: make is
  semi-declarative, good for ~80% of projects; its strengths are the DAG +
  mtime rebuild and *simple* PHONY tasks; "easy escape hatches … make as the
  entry point for a variety of bash scripts." I.e. thin make + scripts, not
  logic in make.

## Decision

Adopt a Makefile convention for keur (and any IaC/tooling repo keur gates),
expressed as **maturity-gated rules** so a prototype is left alone and a
"defined"+ project gets the tips. The convention is "thin, derived, honest":

### The conventions, mapped to maturity (ADR-026)

| # | Convention | Antipattern it blocks | Maturity | Verdict |
|---|------------|-----------------------|----------|---------|
| 1 | Has a working default/`build` target | Makefile that does not build | 1 prototype | build-breaker |
| 2 | `.PHONY` declared for non-file targets (build/test/clean/…) | a file named `test` silently skips the target | 2 startup | warn |
| 3 | Targets stay thin: ≤5 inline shell lines (excl. `@echo`, `$(MAKE)`); more → a script | business logic living in the Makefile | 3 defined | warn |
| 4 | Objects DERIVED from sources (`wildcard`/`patsubst` + ONE pattern rule); no hand-maintained per-source compile rule | the ~30-rule drift that broke on a new source | 3 defined | warn |
| 5 | Auto-dependency tracking (`-MMD -MP` + `-include $(DEPS)`) | stale builds when a header changes (ADR-008) | 4 hardened | warn |
| 6 | No `curl \| sh` / unpinned downloads in a recipe | supply-chain exposure in the build | 1 (secrets/build-breaker band) | block |

Rationale for the bands:
- 1 (prototype): only build-breakers and supply-chain — a throwaway must still
  build and must not pull untrusted code. (#6 reuses the existing supply-chain
  rules SCA-029/030/063, listed here for completeness, not re-implemented.)
- 2 (startup): correctness-of-invocation basics (`.PHONY`).
- 3 (defined, keur's own level): maintainability — thin targets + derived
  objects. This is where keur itself lives, so keur must pass these; fixing the
  30-rule Makefile was conformance, not polish.
- 4 (hardened): build correctness under change (auto-deps).
- 5 (mission-critical): hermetic/pinned toolchain — a later addition.

### Scope: thin make, not no make

keur is a C++ project with no higher build tool, so the Makefile IS the build —
"make as a pure alias layer" (cpm ADR-010) applies only partially. The line we
draw: make expresses the DAG (what depends on what) + compile/link + simple
PHONY tasks; anything that is a multi-step *procedure* goes to `scripts/`.
Deriving objects from sources is DAG expression (fine in make); a 10-line shell
pipeline in a recipe is a procedure (extract it). We do NOT move to CMake/Bazel:
ADR-002 chose against build-time complexity, and make's mtime DAG + `-MMD` cover
keur's needs.

### These are rules, and they are feasible

Per ADR-009 each becomes a `.rule` (data), targeting `filenames: Makefile`
(the engine already supports filename targets, e.g. SONAR-006, 12F-019). The
drift SHAPES are regex-detectable:

- #4 (derived objects): fire when the Makefile contains more than a threshold of
  hand-written object recipes — the shape `$(…)/….o: ….cpp` immediately followed
  by a `$(CXX) … -c` recipe line. One such pair is normal (a special); many is
  the antipattern. A `pattern` rule flags the shape; a count threshold is the
  follow-up (an `extract`/metric engine, ADR-004 stage 2, if one lands).
- #3 (thin targets): the >5-inline-shell-lines count. This is a METRIC, not a
  single-line regex — it needs line counting per target, which the current
  `pattern` engine cannot do. So #3 is **not** a pure-pattern rule today; it is
  either (a) kept as the existing cpm shell check invoked as an external scanner
  (ADR-019), or (b) a follow-up block/extract engine. Be honest: ship #2, #4, #6
  as pattern rules now; #3 and the #4 *count* as follow-ups.
- #2 (`.PHONY`): `absence` engine — `.PHONY` must be present when file-looking
  targets exist. Feasible now.
- #5 (`-MMD`): `absence` — if the Makefile compiles (`$(CXX) … -c`) but never
  mentions `-MMD`/`-include`, flag it. Feasible now.

Every rule's message states the fix (ADR-035), e.g. "extract this recipe to
scripts/… — make orchestrates, scripts implement" with a `@see` to this ADR.

## Why not the alternatives

- **No convention, just clean keur's Makefile once**: the 30-rule drift returns
  the next time someone adds a source. Fix without control is temporary
  (sustainable-change principle).
- **One hard rule "make must be a thin wrapper"**: too blunt and wrong for a
  C++ build — some logic (the DAG, pattern rule) belongs in make. Maturity
  bands + the "procedure → script" line are the precise cut.
- **Reimplement cpm's shell-line counter in the engine now**: #3 needs a metric
  the pattern engine lacks; reinventing it before the engine supports counting
  is premature. Reuse cpm's check via the external-scanner seam (ADR-019) until
  a counting engine exists.

## Consequences

- **Positive**: keur gains an opinion on Makefiles scaled to the maturity the
  dev chose — prototypes untouched, "defined"+ repos nudged. Consistent with the
  house standard (apex-cli/cpm/helmet) and the literature (thin make + scripts).
- **Positive**: keur's own Makefile is held to its own maturity-3 rules — it now
  derives objects and (follow-up) keeps targets thin. keur guards itself.
- **Negative / cost**: three ship-now rules (#2, #4-shape, #5) + wiring #3 and
  the #4 count as follow-ups once a counting/metric engine exists. #6 is already
  covered by SCA-029/030/063 — just cross-referenced, not duplicated.
- **Honest limit**: the pattern engine cannot count lines, so the "≤5 inline
  shell lines" convention is not a native rule yet; this ADR says so rather than
  pretending otherwise.

## Follow-ups

1. `rules/keur-internal/` (or a shared `rules/quality/`): the ship-now Makefile
   rules — `.PHONY` present (#2, maturity 2), derived-objects shape (#4,
   maturity 3), auto-dep present (#5, maturity 4). `filenames: Makefile`,
   messages with the fix + `@see ADR-044`.
2. Cross-reference the existing SCA-029/030/063 as the maturity-1 build
   supply-chain band (#6) — no new rule.
3. When a counting/metric engine lands (ADR-004 stage 2), add #3 (≤5 inline
   shell lines) and the #4 count threshold natively; until then wire cpm's
   `check-makefile.sh` via the external-scanner seam (ADR-019).
4. Note in ADR-026 that keur's maturity labels are CMMI-INSPIRED, not literal
   CMMI (prototype/startup/hardened/mission-critical mix lifecycle and posture);
   this ADR's bands follow keur's labels, and the apex-cli
   learn→guide→guard→enforce verbs map onto the same axis.
