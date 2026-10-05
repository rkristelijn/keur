# ADR-033: Seed refinement is CPM-only; rejected near-misses are measure-only

## Status

Accepted (2026-09-26)

## Context

ADR-029 added seed-injected learning: `optimise.sh seed-iterate` feeds the loop
a proven starting point (attempt-0) from two sources — CPM rules and recovered
rejected near-misses — then, if the seed does not pass the gate as-is, hands it
to the LLM to refine (with a good-example shape and a real missed corpus snippet
as steering).

On the first real run this worked for CPM seeds but failed pathologically for
**rejected near-misses**. Observed on seed `SEED-REJ-1D920FC6`:

```text
[seed 4] seed needs refinement: recall did not rise (0.215→0.215); precision
         too low (0.099); holdout precision 0.135 (fp=96) — generalises as noise
[seed 4] refine attempt 1/4 (retry: <same reason>)
[seed 4] refine attempt 2/4 (retry: <same reason>)
[seed 4] refine attempt 3/4 (retry: <same reason>)
[seed 4] refine attempt 4/4 (retry: <same reason>)
```

The gate measurement is genuine — `eval/measure.sh` runs the compiled engine
(`bin/keur-rules --dir <repo>`) over the whole corpus, joins its findings with
the proven-tool ground truth at (repo, file, category), and computes
recall/precision (see `measure.sh` header). So each attempt really was executed;
it really produced zero new true positives four times.

### Root cause (finding)

A **rejected near-miss** is a *bare regex* harvested from `attempts.tsv`. We keep
its category but NOT the specific false-negative it originally chased (that link
was never recorded). In the refine loop we therefore show the model a *random*
same-category snippet (via `seed_snippet_for`). The model cannot reconcile an
unrelated snippet with the bare regex, so it emits variants that all score
identically — no progress, four wasted LLM calls, and (because the score is
unchanged) identical feedback each attempt.

By contrast, **CPM seeds are complete, coherent rules** (id, title, target,
patterns, message). The model can meaningfully adapt them, and the same-category
snippet is a plausible target for that rule's intent.

The two seed sources have fundamentally different information content, so they
warrant different handling.

## Decision

Split `seed_iterate_once` behaviour by `source`:

- **`rejected-near-miss` → measure-only.** Measure the regex as-is against the
  corpus. If it passes the gate, accept it (these seeds are already
  "generalising" by construction — that is why they were harvested). If it does
  not pass as-is, **reject immediately; do not enter the LLM-refine loop.**
  There is no coherent target to refine toward, so refinement only burns tokens
  and produces the identical-reject pattern above.

- **`cpm` → measure-then-refine (unchanged from ADR-029).** Measure as-is; if it
  passes, accept; otherwise refine via the LLM with the good-example shape and a
  real missed snippet, under the normal retry-feedback + gate loop.

## Consequences

- Near-miss seeds stop wasting LLM calls on unrefinable cases; they contribute
  only when they genuinely pass the gate on their own merit.
- CPM adoption — the high-value path (789 candidate rules) — keeps its full
  refine loop, where the seed carries enough context for the LLM to help.
- The gate is unchanged; the split only decides *whether* to invoke the LLM, not
  *how* acceptance is judged. Benchmark still decides.
- Logging distinguishes the two reject reasons: `as-is-only` (near-miss failed
  its single measure) vs `gate` (CPM seed failed after refinement), so the
  `attempts.tsv` trace stays diagnosable.

## Follow-up (not done here)

To make near-miss refinement viable later, the harvester would need to link each
near-miss back to the FN it originally targeted (recorded at attempt time in a
future `attempts.tsv` column, or joined from `loop.log`). With a real target
snippet, near-misses could re-enter the refine loop like CPM seeds. Until then,
measure-only is the honest, non-wasteful behaviour.

## Alternatives considered

- **Give near-misses a random same-category snippet and refine anyway**:
  rejected — this is exactly the failing behaviour that motivated the ADR.
- **Drop rejected near-misses entirely**: rejected — some DO pass as-is (they
  were generalising regexes wrongly killed by the static overfit guard); the
  measure-only path still captures that value at near-zero cost.
- **Record the FN target now and wire full refinement**: deferred — larger change
  (new attempts.tsv column + harvester join); noted as follow-up.
