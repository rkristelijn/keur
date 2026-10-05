# ADR-030: Corpus revalidation — re-running the whole rule set through the gate

@id ADR-030

## Status

Proposed (options laid out; decision open) · 2026-09-26 · Coverage: none
· Relates to: ADR-023 (synthetic corpus), ADR-027 (measurement granularity),
ADR-029 (seed-injected learning), ADR-026 (rule maturity), ADR-028 (keur is the successor)

## Context

Every rule in `rules/learned/` (and every eval-gated `source: cpm` import) earned
its place by passing the acceptance gate **once**, against the ground truth that
existed *at that moment*. But three things drift underneath a rule after it lands:

1. **The corpus grows.** New positive/negative cases are added (`eval/corpus/`,
   ADR-023 synthetic generation, harvested repos). A rule that raised precision
   on the old corpus may now fire on a newly-added safe case — a regression that
   nothing re-checks.
2. **The ground truth is rebuilt.** `groundtruth.jsonl` has been rebuilt more than
   once (`.bak-before-rebuild-*`, `.bak-*` files in `eval/corpus/`). When the
   labels move, every past pass/fail decision was made against labels that no
   longer exist.
3. **The gate itself changes.** The overfit heuristic (`regex_generalises`), the
   precision floor (`KEUR_MIN_PRECISION`), and the engines all evolve. ADR-029
   already notes the overfit guard has false positives that a follow-up should
   fix — when it does, rules previously rejected *or accepted* deserve a re-judge.

The failure mode if we do nothing: **"every rule is gated" silently degrades into
"every rule was gated once, against a corpus that no longer exists."** That is
precisely the cpm mistake (unmeasured corpus) creeping back in through time
rather than through bulk-import. The whole point of keur is that a rule you
cannot *currently* measure is a rule you cannot *currently* trust.

The question this ADR answers: **should the whole corpus be re-run through the
gate periodically, and if so — when, triggered by what, and what does a
regression do?**

## Decision

**Yes — full-corpus revalidation must be a first-class, scheduled operation, not
an ad-hoc afterthought.** This ADR does not fix the exact cadence; it records the
options and a recommended default, so the choice is deliberate.

### The invariant (not optional)

A `revalidate` run re-measures **every active rule against the current ground
truth** and produces a per-rule verdict: still-passing / newly-regressed /
now-redundant. It uses the *same* acceptance gate as the learn loop — the
benchmark stays the sole referee (ADR-027). It never edits ground truth (the
`keur-design-rationale.md` separation holds: the detector never rewrites its own
yardstick).

### Option A — Trigger-based (recommended default)

Revalidate automatically whenever a gate-input changes, not on a clock:

- ground truth rebuilt (`groundtruth.jsonl` hash changes), OR
- the acceptance gate changes (overfit heuristic / precision floor / engine set),
  OR
- a batch of N new rules lands (e.g. every 25 accepts).

*Why recommended:* it ties the cost to the actual risk. A rule can only silently
rot when one of its inputs moved; A re-runs exactly then, and never wastefully.
*Cost:* needs the inputs fingerprinted (a hash of `groundtruth.jsonl` + gate
version) so "did an input change?" is cheap and deterministic.

### Option B — Scheduled (calendar cadence)

A full revalidate on a fixed cadence (e.g. weekly, or every release). Simple,
predictable, easy to wire into CI. *Downside:* decoupled from risk — it re-runs
when nothing changed (waste) and can miss a mid-week ground-truth rebuild until
the next tick. Best as a *floor* under Option A ("at least weekly, plus on any
input change"), not as the only mechanism.

### Option C — Continuous (part of the learn loop itself)

Every learn iteration first re-measures the existing corpus before proposing a
new rule, so regressions surface within one cycle. *Upside:* nothing rots for
longer than one iteration. *Downside:* makes each iteration heavier and couples
"grow the corpus" with "audit the corpus" — the two have different rhythms, and a
long-running loop that also revalidates 1000 rules per iteration is slow. Better
as an occasional `seed-iterate`-style mode than the default `iterate`.

### Option D — Do nothing / manual only (rejected)

Revalidate only when someone remembers to. *Rejected* — this is the exact drift
the ADR exists to prevent; "someone remembers" is not a control.

### What a regression does (policy, same for A/B/C)

When revalidation finds a rule that no longer passes:

1. **Quarantine, don't delete.** Move it to `rules/learned/quarantine/` (or flag
   `maturity:` down, ADR-026) so it stops gating but stays inspectable — a
   deleted rule loses the information about *why* it regressed.
2. **Record the regression** in `eval/history/` with the old vs new
   precision/recall and the case that broke it (mirrors the `optimiser.jsonl`
   decision log — auditable, reproducible).
3. **Offer it back to the learn loop as a seed** (ADR-029): a regressed rule is a
   near-miss with known provenance — an ideal attempt-0 for "adapt this so it
   passes the *new* ground truth too."

### Recommended default

**Option A (trigger-based) with Option B as a floor**: revalidate on any
ground-truth or gate change and on every batch of accepts, and additionally at
least once per release. Regressions quarantine + log + re-seed. This keeps the
"every rule is *currently* measured" guarantee true without paying for a
full re-run when nothing moved.

## Consequences

**Positive**

- The "every rule is gated" claim stays *true over time*, not just at insert-time.
- Ground-truth rebuilds and gate changes become safe: their blast radius on the
  existing corpus is measured, not assumed.
- Regressions become a tracked, re-seedable event, not a silent loss of precision.

**Negative / cost**

- Needs input fingerprinting (ground-truth hash + gate version) to make
  trigger-based cheap and deterministic.
- A full revalidate over ~1000+ rules is not free; it belongs in the `ci` tier /
  a background job, never in the pre-commit path.
- Quarantine adds a rule lifecycle state the manager TUI (ADR-manager) and the
  `maturity` model (ADR-026) must both understand.

## Alternatives considered

- **Trust insert-time gating forever (Option D):** rejected — reintroduces the
  unmeasured-corpus failure through drift.
- **Re-generate the whole corpus from scratch each time instead of revalidating:**
  rejected — that changes the yardstick and the rules at once, so you cannot tell
  a rule regression from a corpus change. Revalidation holds the corpus fixed for
  the duration of the run.
- **Delete regressed rules outright:** rejected in favour of quarantine — a
  regressed rule carries information (its provenance + the breaking case) that is
  the ideal seed for re-learning it against the new ground truth.

## Open questions

- The exact accept-batch size N for the trigger, and whether `criticality`
  (ADR-011) should force a stricter revalidation cadence for high-tier profiles.
- Whether revalidation should also re-judge *rejected* candidates (the overfit
  false-positive pool from ADR-029) when the gate improves, not only active rules.
- Where the ground-truth-hash + gate-version fingerprint lives (`eval/.run/`?)
  and how CI reads it to decide "an input changed → revalidate".
