# KEUR — Self-improving evaluator (CPM-Eval loop)

> The laboratory. KEUR is the product; CPM-Eval is the scientific yardstick that
> proves KEUR actually gets better. The **benchmark** decides improvement — never the LLM.

## Principle

> Do NOT optimise for "number of findings". Optimise for **classification quality**.
> A detector reporting 10,000 findings is not better than one reporting 500 if most of
> the 10,000 are false positives.

Metrics: TP, FP, FN, TN → precision, recall, F1. But **F1 alone is not the acceptance
criterion**. Use an explicit, stable weighted objective:

```text
score = w_p·precision + w_r·recall + w_s·regression_safety
regression_safety = 1 − regression_count / total_known_cases
```

Weights are configurable (`[optimiser]` in keur.toml). For a **security** detector,
false negatives deserve a higher penalty; for a **developer-facing linter**, false
positives do (noise makes it unusable). The point is that the weighting is *explicit*.

## Loop

```text
baseline = current_detector
for iteration in 1..MAX:
    result   = evaluate(baseline, VALIDATION)
    failures = analyse(result.fp, result.fn, result.regressions)
    context  = build_context(result.metrics, failures,
                             history.summary(), history.relevant_attempts())
    proposal = ollama.generate(context)          # smallest possible change + rationale
    candidate = apply_patch(proposal.patch)
    cand_res  = evaluate(candidate, VALIDATION)
    if acceptance_gate(baseline, cand_res):
        baseline = candidate
        persist("accepted", cand_res, proposal)
    else:
        persist("rejected", cand_res, proposal)
    update_historical_summary()
    if convergence_detected(): break
# periodic, held-out:
evaluate(baseline, EVALUATION)   # never seen by Ollama during optimisation
```

## Immutable evaluation set (most important design decision)

```text
eval/datasets/
    train/         # candidate generation may use this
    validation/    # accept/reject decisions run here
    evaluation/    # HELD OUT — never shown to Ollama, never used to propose changes
    regression/    # known-good cases that must never break
```

The optimiser must **never** see or modify its own ground truth. Otherwise it "learns
to pass its own tests" (overfitting). The final evaluation corpus stays hidden.

## Acceptance gate (hard constraints — more important than the LLM)

A candidate replaces the baseline only if ALL hold (thresholds configurable):

```text
precision(cand) >= precision(base) - 0.01
recall(cand)    >= recall(base)
critical_FN(cand) <= critical_FN(base)
regressions(cand) == 0
weighted_score(cand) > weighted_score(base)
```

## Pareto thinking

Don't collapse to one number too early. Track: precision, recall, F1, critical FN,
high-severity FN, FP/KLOC, runtime, rules triggered, regressions. Report trade-offs
("recall +4.2%, precision −0.3%, critical FN −7, FP +2"), let the acceptance policy
decide if the trade-off is acceptable.

## Ollama's role: analyst, not authority

Ollama may: analyse metrics/FP/FN/regressions/history → identify the dominant failure
mode → propose the **smallest** change → predict regressions → produce a patch +
rationale + the next iteration's prompt. It may **not** decide acceptance.

**Minimal-change principle**: each iteration changes ONE conceptual thing (add a
contextual exclusion, extend one rule, reduce one FP class). Makes regressions
attributable and the process explainable.

**Two-layer memory** (never concatenate every log into the prompt):

- short-term: current iteration findings
- long-term: compact summary — accepted/rejected changes, known failure modes, rules
  that caused regressions, patterns that consistently produce FP/FN

`eval/history/index.json` holds one compact record per iteration:

```json
{ "iteration": 17, "precision": 0.91, "recall": 0.84, "f1": 0.875,
  "false_positives": 31, "false_negatives": 17, "regressions": 2, "accepted": true }
```

## Confidence ≠ correctness

Keep the **finding** confidence (evidence-based: pattern strength + context +
corroboration − ambiguity) separate from any notion of "how good Ollama thinks its own
patch is". They are different concepts and must not be merged into one score.

## Safety controls

`MAX_ITERATIONS`, `MAX_RUNTIME`, `MAX_LOG_SIZE`, `MAX_CANDIDATES_PER_ITERATION`, and
convergence (stop when no accepted improvement for N iterations AND precision/recall
gains < epsilon AND no new failure classes).

## Detector evolution (benchmark stays stable)

```text
L0 regex → L1 regex+context → L2 AST rules → L3 AST+semantic → L4 local-LLM class → L5 hybrid
```

The CPM-Eval benchmark and acceptance gate remain in place across all levels, so the
benchmark is the stable foundation while the detector implementation evolves.

## Sonar / external tools

Study Sonar's architecture and (license-permitting) rule metadata + test cases as **one
reference implementation and an evidence source** — do not clone it. Note: analyzers
released after 2024-11-29 fall under Sonar's Source-Available License (SSALv1), so
"Sonar rules are all LGPL open-source" is no longer accurate. Use Sonar's disagreements
with KEUR as FN/FP candidates for the corpus.
