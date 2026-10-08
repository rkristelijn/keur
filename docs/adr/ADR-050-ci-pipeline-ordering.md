# ADR-050 — CI pipeline ordering: fail-fast, cheap-to-expensive stages

@id ADR-050
**Status**: proposed · **Date**: 2026-10-08 · **Coverage**: none (`.github/workflows/ci.yml` to be restructured) · **Relates to**: ADR-002 (single C++ binary — compile is a precondition for the unit tests), ADR-005 (platform abstraction — the cross-platform matrix), ADR-006 (word-size portability — the arm32 build), ADR-018 (cpm/keur second gate — dogfooding), ADR-041 (tiered CI)

## Context

The current `ci.yml` has three jobs: `build` (a Linux/macOS/Windows matrix that
compiles, then runs `make test`, then `smoke.sh`), `build-arm32` (QEMU), and
`dogfood` (`needs: build`, rebuilds keur and runs the self-checks + the diff
gate). The observed ordering problems:

1. **No lint stage.** There is no cheap static gate (clang-format/clang-tidy,
   shellcheck on the e2e scripts, rumdl on the docs) running first. A formatting
   or lint error is only ever caught — if at all — after a full three-OS compile.
   (There is not even a `.clang-format` yet; tracked separately in TODO.)

2. **The expensive cross-platform matrix runs first; the cheap, repo-specific
   gates run last.** `dogfood` (`needs: build`) waits for the whole 3-OS matrix
   before running the self-check and diff gate — the gates most likely to catch
   a repo-specific regression, and the cheapest to run.

3. **`dogfood` recompiles.** `build` already compiles on Linux; `dogfood` does
   `make build` again. Wasted minutes.

4. **e2e is hidden and doubled.** `make test` already runs unit + integration +
   **e2e** (src/keur/Makefile `test: test-unit test-integration e2e`). The build
   job then runs `smoke.sh` separately, which overlaps the e2e (both exercise
   version/scan/check). e2e is not a visible, named stage — it is buried inside
   the build job and partly run twice.

A note on the hard constraint: keur is a single C++ binary (ADR-002). The unit
tests are a COMPILED test binary, so "run unit tests before building" is not
possible — the compile *is* the precondition. The win is not "tests before
build"; it is "compile + unit/integration once on the cheapest target as the
fast gate, cheap static checks before that, and the expensive cross-platform
matrix last".

## Decision

Restructure `ci.yml` into ordered, fail-fast stages, cheap → expensive:

@id ADR-050.stages

1. **lint** (seconds, no build) — clang-format `--dry-run --Werror` (once a
   `.clang-format` lands), shellcheck on `tests/e2e/*.sh` and `scripts/*.sh`,
   rumdl on the docs. First gate: a style error must not cost a 20-minute matrix.
2. **build-test** (minutes, Linux only) — `make build` + `make test-unit` +
   `make test-integration`. The fast correctness gate on one cheap runner.
3. **e2e** (`needs: build-test`) — a VISIBLE, named stage running `make e2e`
   (or `tests/e2e/run-e2e.sh`) against the Linux binary built in stage 2 (reuse
   the artifact, do not recompile). This is the behavioural gate the user feels.
4. **dogfood** (`needs: build-test`) — self-check (architecture + portability)
   and the diff gate (`keur check` over the PR/push diff), reusing the stage-2
   binary instead of rebuilding.
5. **cross-platform** (`needs: e2e` — only pay for it once Linux is green) —
   the macOS/Windows build + smoke matrix and the arm32 QEMU build, in parallel.
   These are the slow, portability-proving jobs; they run last so a Linux-side
   failure fails the pipeline in minutes, not after the full matrix.

Stages 3 and 4 run in parallel (both only need stage 2). Stage 5 gates on 3 so a
behavioural failure stops the pipeline before the expensive matrix.

### e2e as a named stage, and reuse the binary

e2e stops being an implicit side effect of `make test` inside the build job. It
becomes stage 3 with its own name in the UI, running `make e2e` against the
compiled Linux binary (uploaded as an artifact by stage 2, downloaded here). The
redundant `smoke.sh` run in the build job is dropped — e2e covers the same
commands with more depth. `smoke.sh` stays as the cross-platform post-compile
check in stage 5 (it is the portable "does the binary run at all" probe).

## e2e coverage assessment (asked 2026-10-08)

Is the e2e suite still good coverage? Mostly yes, with two gaps.

**Covered well** (14 suites, real behaviour not smoke): `check` (clean→PASS,
staged key→BLOCK, empty→PASS), `scan` (JSONL shape, `--format compact`,
single-file regression, clean vs dirty), `hook`, `policy`, `rules`, `config`,
`level`, `difficulty`, `manager`, `version`, plus the two dogfood self-checks
(selfcheck, portability) and `sonar`.

**Gaps to close** (follow-up, not blocking this ADR):
- No e2e for `doctor` (a good, non-interactive e2e candidate). `learn`/`train`/
  `settings`/`tune` are interactive/TUI, so they stay on the doctest side.
- **The VS Code stderr bug was NOT caught by e2e.** No e2e asserts that a PASS
  writes to stdout and nothing to stderr. A unit test now covers it
  (`a PASS writes to stdout, not stderr`), but an e2e assertion on the real hook
  invocation ("hook PASS prints nothing to stderr") would guard the actual
  editor condition end to end. Add it.

## Consequences

**Positive**
- Fail-fast: a lint or Linux-correctness error stops CI in seconds/minutes, not
  after a three-OS matrix.
- Cheapest, most repo-specific gates (lint, dogfood) run early.
- e2e is a visible, named stage, run once, against the already-built binary.
- Less wasted compute: no recompile in dogfood, no doubled e2e/smoke.

**Negative / risk**
- More jobs = more YAML and more inter-job artifact passing (upload/download the
  Linux binary). Mitigated: it is standard actions/upload-artifact plumbing.
- Reordering the pipeline is itself a change that must be verified on a branch
  (a green run) before trusting it — do it as its OWN MR, not mixed into a
  feature change whose merge depends on CI being green.

**Rollout**
- Land the `.clang-format` first (separate TODO) so the lint stage has something
  to enforce; until then the lint stage runs shellcheck + rumdl only.
- Restructure `ci.yml` on a dedicated branch; confirm a full green run before
  merging. Keep `smoke.sh` for the cross-platform stage.

## Testing the pipeline locally (before pushing)

Two levels, so a push is not a debug-by-CI loop:

- **Stage content** — `scripts/ci-local.sh` runs the stages in this ADR's order
  on the local machine (lint → build + unit/integration → e2e → dogfood), fail-
  fast, no Docker. `--fast` skips e2e. The cross-platform matrix is the one thing
  it cannot prove and is skipped with a note. This catches ~90% of failures
  (compile, test, lint, dogfood) in seconds-to-minutes.
- **Workflow graph** — `act -l` (nektos/act) prints the job graph and stages, so
  a reordering can be validated structurally before pushing: today it shows
  `stage 0: build, build-arm32` then `stage 1: dogfood` — exactly the inversion
  this ADR fixes (expensive matrix first, cheap dogfood after). `act -j <job>`
  can run a Linux job in Docker; the macOS/Windows/arm32 matrix still needs real
  CI. Use `act -l` after reordering to confirm the new `needs:` graph.

## Open questions

- Do we gate stage 5 (cross-platform) on e2e, or run it in parallel from stage 2
  to shorten wall-clock at the cost of some wasted matrix runs on a failing diff?
  Lean fail-fast (gate on e2e) unless matrix latency becomes the bottleneck.
- clang-tidy in the lint stage now, or after `.clang-format` settles the style?

## Sonar gate (part of lint)

The SonarCloud analysis is the second maintainability gate (ADR-018). It belongs
conceptually in the lint stage, but two things need care so it reports signal,
not noise:

- **Reject C++23 suggestions against a C++20 codebase.** Sonar flags `\033` →
  `\o{33}` (since-c++23) and `std::format` (fine) and `std::byte` on low-level
  decoders. keur builds `-std=c++20` on gcc/clang/MSVC (ADR-002), so `\o{...}`
  is a FALSE POSITIVE — the same class as cpp:S6659 that KEUR-QUAL-011 already
  documents. These must be suppressed at the project level in Sonar, not "fixed".
- **Act on genuine new debt.** Cognitive-complexity / nesting findings on
  freshly added code (e.g. the ADR-049 unwrap driver and UTF-8 validator) are
  real and should be refactored when introduced — extract helpers so no function
  exceeds the complexity/nesting bar. Dogfooding (KEUR-QUAL-004 max-nesting,
  KEUR-QUAL-005 max-lines) catches the same shapes locally.

