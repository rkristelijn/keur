# ADR-055 — 4A tests: Arrange, Act, Assert, Annihilate (phase annotations)

@id ADR-055
**Status**: proposed · **Date**: 2026-10-08 · **Coverage**: partial (RAII `TempDir` lands the Annihilate mechanism; phase annotations + the check to follow) · **Relates to**: ADR-010 (traceability verbs), ADR-047 (data-driven verb registry — this adds a verb *kind*), ADR-002 (single binary), ADR-052/054 (recent test additions that would carry the annotations)

## Context

keur's tests follow Arrange-Act-Assert (AAA). Two weaknesses surfaced
(2026-10-08):

1. **AAA has no clean-up phase, so clean-up leaks.** A scenario arranges a
   tempdir, acts, asserts, and calls `cleanup_dir()` on the *last line*. When an
   earlier `REQUIRE` fails, doctest aborts the test via exception and that last
   line never runs — the tempdir leaks. Measured: **263 stale
   `/tmp/keur_test_*` dirs**. Worse, clean-up is not symmetric with set-up: 70
   scenarios create a tempdir but only 65 call `cleanup_dir` — the two drifted.

2. **The phases are implicit.** Nothing marks where Arrange ends and Act begins,
   and nothing can detect a test that never asserts, or that hides logic between
   Act and Assert. The structure is convention, not something keur can measure
   (ironic for a tool whose point is measurable structure).

## Decision

Adopt **4A**: Arrange, Act, Assert, **Annihilate**. The fourth A is a
first-class phase, not an afterthought — the symmetric counterpart of Arrange:
*whatever Arrange builds, Annihilate tears down, and it runs on every exit path,
pass or fail*. (We say "Annihilate", not "teardown": four equal A's, one name
per phase.)

### 1. Annihilate is symmetric with Arrange — bind them with RAII

@id ADR-055.symmetry

A resource acquired in Arrange is released in Annihilate by the SAME object, so
the two can never drift apart and Annihilate cannot be skipped. The concrete
vehicle is `keur::test::TempDir` (tests/test_helpers.h): its constructor is
Arrange (make the dir), its destructor is Annihilate (remove it), which C++ runs
on normal return AND on the exception path a failed `REQUIRE` takes. This
replaces the leak-prone trailing `cleanup_dir()`. Any future per-test resource
(temp file, env var, fake clock) follows the same RAII shape.

### 2. The four phases are grep-able annotations (a new verb *kind* in ADR-047)

@id ADR-055.phases

Add four phase verbs to the registry (`docs/design/traceability-verbs.tsv`),
with a new `kind: phase` (they mark intra-test structure; they do not reference
an `@id` like the existing concept verbs):

| verb | marks |
|------|-------|
| `@arrange` | set-up: fixtures, inputs, the system under test |
| `@act` | the single call under test |
| `@assert` | the expectations |
| `@annihilate` | explicit clean-up when it is NOT already RAII-automatic |

`@annihilate` is **optional when a RAII guard already owns clean-up** (the common
case — `TempDir` makes it invisible and automatic); it is written only when a
test does manual clean-up, so the phase is still visible. Arrange/Act/Assert are
the structural spine.

### 3. The annotations must earn their place — they feed a check

@id ADR-055.check

Annotating 290 scenarios is noise unless it powers a control. The target rule
(engine side, later): a `SCENARIO` body should contain an `@act` followed by an
`@assert`, with no bare statements doing real work *after* the assert except an
`@annihilate` block. That catches two real smells — a test that never asserts,
and logic sneaking in after the assertions. The annotations make those
grep-able / rule-detectable; without the check they are decoration, so the check
ships with (or before) a broad roll-out.

## Rollout (incremental, with the test split)

The 290 scenarios are being split out of `keur_test.cpp` per feature (step 0 —
shared helpers — already done). Each file lifted out gets its phase annotations
*in the same iteration*, so the 290 are spread over the split work, never a
single mega-diff. `TempDir` replaces `create_temp_dir()` + `cleanup_dir()` in a
scenario as that scenario is touched.

## Consequences

**Positive**
- Clean-up is guaranteed and symmetric with set-up (no more 263-dir leaks).
- Test phases become visible and, with the check, enforceable — keur measures
  its own test structure, consistent with its mission.
- The verb registry (ADR-047) stretches to a new `kind` without a code edit —
  just rows + the meter already reading the TSV.

**Negative / risk**
- Four markers per test is visual overhead. Mitigation: Arrange is often one
  line and needs no marker on a trivial test; `@annihilate` is usually absent
  (RAII). Keep markers for where a phase is non-obvious, not as mandatory
  boilerplate on a three-line test — the check enforces *assert-after-act*, not
  "every test has four comments".
- A roll-out across 290 tests is large; mitigated by riding the split.

## Open questions

- Is `@arrange` mandatory, or implied by "everything before `@act`"? Lean:
  `@act` + `@assert` are the required pair; `@arrange`/`@annihilate` are written
  only when they add clarity.
- Should the check live as a keur rule (`block-absence`-style over a SCENARIO
  body) or in `doc-coverage.sh`? Lean: a rule, so it runs in the normal gate.

## The assert-after check needs a stronger block engine (found 2026-10-08)

Prototyping the check as `block-absence` rules exposed two engine limits that
must be fixed before the check can ship broadly — the check is **deferred**, not
abandoned:

- **C++/doctest (`KEUR-TEST-001`)** false-positives on a `SCENARIO` whose body
  holds a raw string literal `R"({ ... })"` with JSON braces: block-absence
  counts `{`/`}` to find the block end, and the raw-string braces end the block
  early, before the real `CHECK`s. `skip_strings` does not recognise C++ raw
  strings. Confirmed on `keur_test.cpp` lines 369/486 (both DO assert).
- **JS/TS (`TEST-046`)** under-fires: an `it('x', () => { ... })` block start is
  immediately followed by the arrow-function brace, and the block-absence
  brace/skip-strings interaction misses the body, so an assertion-less `it` is
  not flagged. The multi-framework intent (jest/vitest/mocha/cypress — the "many
  runners" reality) is right; the engine mechanics are not there yet.

The honest fix is in the engine, not in rule tweaks: block-absence needs
raw-string awareness (C++) and reliable brace counting across arrow-function
bodies (JS). Until then both rules are kept OUT of the active set (parked under
their category with maturity set so they do not gate), and this is tracked as
engine work. The 4A *mechanism* (TempDir, phase annotations) stands on its own
and ships now; only the automated *check* waits.
