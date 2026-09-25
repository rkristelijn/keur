# ADR-027: Measurement granularity for the learning loop

## Status

Accepted (2026-09-25)

## Context

The `keur learn` optimiser grows `rules/learned/` by measuring keur against a
proven-tool ground truth (`eval/measure.sh`) and asking a local LLM to write the
smallest rule that closes the largest **false-negative** cluster. The loop only
learns when there are FNs to close.

An overnight run against a freshly enriched corpus (9 tools, ~9,600 ground-truth
findings, mestjs added) produced **zero new rules** across 773 attempts. Recall
sat at **1.0 with FN=0** the entire run, so every LLM proposal was rejected
(nothing to gain on recall; precision could not rise without overfitting).

Root cause: `measure.sh` matches at **`(repo, category)`** granularity. A
ground-truth finding is "reproduced" if keur fires **any** finding of the same
category anywhere in that repo. The ground truth has **200+ distinct semgrep
check-ids** (plus hundreds from other tools) but only **6 categories**
(`secret`, `sast`, `iac`, `cmdi`, `supply-chain`, `sqli`). Those collapse into
~140 `(repo, category)` pairs, all of which keur's ~300 rules already hit. The
measurement literally cannot see what keur misses *within* a category: keur
firing one `sast` rule "covers" all 200 semgrep sast checks in that repo.

So the loop reports "done" while keur in reality misses most specific techniques
the proven tools detect. More findings did not help — they compressed into the
same 6 buckets.

## Decision

Make the match granularity **configurable**, and default the learning loop to a
**finer** key so within-category gaps become real FNs.

- New env `KEUR_MATCH_GRANULARITY`:
  - `category` — legacy `(repo, category)` (kept for backward-compatible audits).
  - `file` — `(repo, file, category)` (**new default for the loop**): keur must
    fire *in the same file* the proven tool flagged, for that category.
- Cross-tool rule-id matching is **not** used: keur (`SEC-041`) and semgrep
  (`react-dangerouslysetinnerhtml`) name rules differently, so a rule-id key
  would be meaningless. File+category is the finest key that is honest across
  detectors that flag the same location by different mechanisms.

Consequence: recall drops sharply (from 1.0 toward a true, low value) because
the many per-file gaps are now visible. That is the point — it gives the loop
real, non-overfit work and lets keur grow past ~300 rules toward the proven
tools' coverage.

## Consequences

- The loop's FN clusters become file-specific and actionable; the acceptance
  gate (`overfit-regex`, precision floor, holdout) still protects quality.
- Historical recall numbers are not comparable across granularities; runs must
  record which granularity they used (written to `measure.json`).
- Pure SCA findings (grype/osv `supply-chain`, CVE-in-dependency) remain hard to
  learn as regex; a follow-up may scope them out via `KEUR_GT_CATEGORIES` so the
  loop focuses on code-pattern categories.

## Alternatives considered

- **Match on tool rule-id**: rejected — no shared taxonomy between keur and the
  proven tools; would require a hand-maintained crosswalk (the exact cpm-style
  drift ADR-009 forbids).
- **Leave at category granularity**: rejected — demonstrably saturated; the loop
  cannot improve keur further on this corpus.
