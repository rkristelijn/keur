# ADR-029: Seed-injected learning (CPM rules + rejected near-misses as attempt-0)

## Status

Accepted (2026-09-26)

## Context

The `keur learn` optimiser (see ADR-027, `docs/design/learning-loop.md`) grows
`rules/learned/` by picking the largest false-negative cluster, asking a local
LLM for the smallest rule to close it, and keeping the candidate only if it
passes the benchmark gate (recall up, precision ≥ floor, generalises on
holdout). The LLM proposes; the benchmark decides.

An overnight run (2026-09-25 23:04 → 2026-09-26 09:06) produced **0 accepts
across ~1900 attempts / 167 gate evaluations**. Baseline never moved
(`recall=0.2148 precision=0.0998`). Analysis of `eval/history/attempts.tsv`
showed the loop oscillating between two failure modes on every FN:

1. **overfit-regex** (static guard): the model pastes a literal from the example
   (a copied sentence, a specific `"encryptionkeys/"` path, a Terraform block
   from one repo) instead of matching the *family*.
2. **precision collapse** (gate): when it compensates by broadening, the rule
   fires on safe code and precision falls below the 0.50 floor.

The root problem is **cold start**: attempt-0 is generated from nothing, so the
model has no good starting point and swings between the two extremes. Yet two
large pools of *proven or near-proven* detection logic already exist:

- **CPM rules** — ~1000 `.rule` files in the sister repo (`~/git/hub/cpm`).
  keur's `.rule` format is cpm-compatible (see `rules/README.md`,
  `source: cpm`); 193 are already imported.
- **Rejected near-misses** — hundreds of regexes in `attempts.tsv` that were
  *generalising* (a real API/sink name like `rejectUnauthorized`, `verify=False`)
  but were killed by the static overfit guard, which cannot tell a memorised
  value from a legitimate long sink name.

## Decision

Add a **seed layer** in front of the loop. Instead of always starting from
nothing, feed the loop a proven starting point ("attempt-0") and let the
existing retry-feedback + gate refine it. The gate is unchanged — the benchmark
still decides — so quality guarantees are identical.

### 1. Seed queue (`eval/extend/seed-harvest.sh`, new file)

Harvests two sources, deduplicated, into `eval/.run/seeds.jsonl`
(one JSON per line: `{source, id, category, path, rule}`):

- **CPM rules** keur does not already have (dedup on rule ID, which keur keeps
  on import). Category mapped to keur's coarse set
  (`security/secret/supply-chain/quality/pii/iac`). Tagged `source: cpm`.
- **Rejected near-misses** whose regex passes a *generalisation* heuristic
  (has regex metachars AND no literal run > 24 chars — so `rejectUnauthorized`
  survives, a copied sentence does not). Wrapped in a minimal `.rule` shell,
  tagged `source: rejected-near-miss`.

Measured harvest (2026-09-26): CPM seen=1004, **kept=789** (dup-id=193,
dup-regex=13, parked=9); rejects seen=1055, **kept=401**; **total queued=1190**.

### 2. Unsupported engines are parked, not dropped

CPM uses four engines keur does not yet implement:
`file-absence`, `file-presence`, `extract-duplicates`, `block-absence`
(9 rules on the current corpus). Rather than silently filter these, they are
written to `eval/.run/seeds-unsupported.jsonl` as an **engine backlog**. This
keeps the option open to add these engines to keur's arsenal (a separate,
larger change: C++ engine + tests + its own ADR) without losing the rules.
keur currently supports `pattern`, `presence`, `absence`
(`src/rules/rule_engine.h`).

### 3. `optimise.sh seed-iterate` (new subcommand, opt-in)

A **separate** subcommand alongside `iterate`/`baseline`. The existing `iterate`
flow is byte-for-byte unchanged; seed behaviour is only reachable via
`seed-iterate` (or `KEUR_SEED_MODE=1`). Per seed:

1. **Measure the seed as-is** against the corpus. If it passes the gate
   unchanged → accept directly into `rules/learned/` (fast win, no LLM needed —
   the common case for proven CPM rules).
2. **Otherwise** hand the seed to the LLM as attempt-0 with the instruction
   "here is an existing rule; adapt it so it also catches this FN and passes the
   gate", then run the normal retry-feedback + gate loop.

`KEUR_SEED_BATCH` (default 5) bounds how many seeds a cycle consumes, so CPM is
adopted in controlled batches rather than all 789 at once.

## Consequences

- **The existing loop is untouched.** `iterate` and `baseline` behave exactly as
  before; seed logic lives in new code paths reachable only by opt-in. Backing
  out = ignore `seed-iterate` and delete the queue files.
- **Attribution + sync preserved.** Accepted CPM seeds keep `source: cpm` and
  their upstream ID, so provenance, upstream-sync and corpus-exclude
  (`grep source: cpm`) keep working (per `rules/README.md`).
- **The gate still governs quality.** A CPM rule that is noisy on keur's corpus
  is rejected or refined like any other candidate; import is not blind trust.
- **Engine backlog is explicit.** `seeds-unsupported.jsonl` is a concrete
  to-do list for extending keur's engine arsenal, instead of a silent drop.
- **Cold-start is no longer the default path to coverage.** The loop can now
  make progress on the same corpus where cold-start scored 0/1900.

## Alternatives considered

- **Relax the overfit guard instead of seeding**: partially valid (the guard
  does have false positives on long sink names — a follow-up should fix
  `regex_generalises`), but it does not address cold start and would not import
  CPM's proven logic. Complementary, not a substitute.
- **Bulk-copy all CPM rules into `rules/learned/`**: rejected — bypasses the
  gate, would inject rules that are noisy on keur's corpus, and breaks the
  "benchmark decides" invariant (ADR-027, learning-loop.md).
- **Filter unsupported-engine rules out entirely**: rejected in favour of
  parking them, so the engine-extension option stays open and auditable.
- **Modify `iterate` in place to optionally seed**: rejected — risks breaking
  the proven loop; a separate subcommand keeps the blast radius zero.
