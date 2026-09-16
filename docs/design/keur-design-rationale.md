# KEUR — design rationale & staged plan

> Design reasoning for KEUR: deterministic guardrails for AI-generated code.
>
> Three repos, three responsibilities:
>
> - **keur** (this repo) — the product: scanners, evidence normalisation, policy
>   engine, hooks, optional local-LLM escalation.
> - **keur-eval** — the benchmark/ground truth: fictional, hand-authored test
>   cases (things KEUR must catch) plus known false-positive cases, each tagged
>   in comments with a code/flag. Immutable; the stable measuring stick.
> - (optional) reference tools (gitleaks/semgrep/trivy/Sonar) as evidence sources.

## One-line positioning

KEUR — deterministic code verification before AI-generated code reaches your repo.
`detectors → evidence → deterministic policy → ALLOW / WARN / BLOCK`.
AI may add evidence; AI may never be the referee.

## Ground truth lives in keur-eval (not in the detector)

The optimisation/measurement never modifies its own ground truth. `keur-eval`
holds:

- **Positive cases** — fictional code snippets containing a defect KEUR must
  detect. Each annotated in a comment with an expected rule/flag, e.g.
  `// KEUR-EXPECT: SQLI-001` or `// KEUR-EXPECT: SECRET-002`.
- **Negative / false-positive cases** — snippets that look defective but are not;
  KEUR must NOT flag them. Annotated e.g. `// KEUR-EXPECT: none  // was FP of RULE-17`.
- **Regression cases** — previously-fixed failures, kept so they never return.

Because the cases are fictional and hand-authored, there is no real secret/PII/
proprietary data in the corpus — it is safe to keep in a normal repo, and the
expected-flag comments make it self-describing and machine-checkable.

Layout:

```text
keur-eval/
  positive/     # must-detect cases (// KEUR-EXPECT: <RULE>)
  negative/     # must-NOT-detect (// KEUR-EXPECT: none)
  regression/   # previously-fixed, must stay fixed
  index.json    # compact map: file -> expected flags
```

## Core principles (non-negotiable)

1. **The benchmark decides improvement, not the LLM.** Hypothesis (LLM) is
   strictly separated from empirical acceptance (deterministic evaluator against
   keur-eval). This stops the system becoming an LLM-driven regex mutation engine.
2. **Findings are evidence, not truth.** Every finding:
   `{source, rule, file, line, severity, confidence, deterministic, fingerprint,
   message}`. A separate policy layer decides block/warn/ignore/escalate.
3. **Deterministic AI-boundary.** Certain → policy; uncertain → LLM → structured
   result → policy. The policy engine is the referee, not the LLM.
4. **Optimise classification quality, not finding count.** Weighted objective
   (precision/recall/regression-safety, configurable), Pareto tracking
   (precision, recall, critical FN, FP/KLOC, runtime, regressions), per-source
   confidence calibration learned from keur-eval.
5. **Immutable evaluation set.** train / validation / evaluation / regression.
   The optimiser never sees the final evaluation slice. Prevents overfitting.
6. **KEUR works without AI.** `keur` = deterministic; `keur --ai=ollama` optional.
   pre-commit fast (<3s), pre-push fuller (<30s), CI unlimited + cross-file + AI.

## Repo layout (keur, already scaffolded)

- `scanners/` — adapters: gitleaks, trivy, semgrep, custom detectors.
- `lib/` — uniform Finding type, fingerprinting, evidence normalisation.
- `policy/` — deterministic decision engine (evidence → ALLOW/WARN/BLOCK).
- `rules/` — KEUR's own detection rules (data, not code).
- `optimiser/` — LLM hypothesis generator + acceptance gate (LATER).
- `eval/` — harness that runs KEUR (and reference tools) against keur-eval.
- `hooks/` — pre-commit / pre-push / CI entry points.
- `bin/` — `keur scan|check|diff|fix|hook|ci`.

## Honest pushback (before over-building)

1. **No ground truth = no measurement.** Build `keur-eval` FIRST (even 100–200
   hand-authored cases). Everything is ceilinged by label quality. Optimising
   without it = optimising toward noise.
2. **LLM-generated regex patches are the weakest link.** Constrain the LLM to
   "classify the dominant failure mode + propose a category of fix," not "emit a
   regex patch." A human/limited generator produces the actual rule; a security
   gate does not accept unreviewed LLM regex.
3. **Scope is the real risk.** The full concept is large. Build the smallest
   useful thing (MVP aggregator + policy + hooks), prove it against keur-eval,
   let it earn expansion. Do not build the optimiser before the benchmark exists.

## Staged plan (build small → prove → expand)

1. **KEUR MVP (no AI):** scanner adapters → normalised Finding → policy engine →
   git hooks. Immediate value as a vibe-coding guard.
2. **keur-eval corpus:** hand-authored positive/negative/regression cases with
   `KEUR-EXPECT` flags. The foundation.
3. **Measure KEUR vs keur-eval:** precision/recall, Pareto metrics,
   AI-invocation-rate, tokens-avoided. Still no self-improvement.
4. **Optimiser loop (only after 1–3):** LLM proposes → deterministic acceptance
   gate (against keur-eval) → persistent iteration history (short-term findings +
   compact long-term summary, never a growing logfile). Convergence stop
   (no accepted improvement for N iters AND Δprecision/Δrecall < epsilon) +
   MAX_ITERATIONS / MAX_RUNTIME / MAX_LOG_SIZE caps.

## Reference tools & Sonar

Treat gitleaks / trivy / semgrep / Sonar / CodeRabbit / Copilot as **evidence
sources and benchmark references**, not truth. Corroboration raises evidence
strength, but shared blind spots exist → agreement ≠ ground truth. Do NOT
reverse-engineer Sonar: post-2024-11-29 analyzers are under SSALv1
(source-available, not free to copy). Run reference tools beside KEUR on
keur-eval; the disagreements are the gold (FN candidates, new-detector
candidates, calibration cases).

## Best operational metric

**AI-invocation-rate / tokens-avoided** — e.g. "9,420/10,000 files resolved
deterministically, 580 needed AI → 94.2% handled without AI." How KEUR
out-performs pure-AI on reliability + cost, with AI as escalation only.

## Detector evolution (benchmark stays constant)

L0 regex → L1 regex+context → L2 AST rules → L3 AST+semantic heuristics →
L4 local-LLM classification → L5 hybrid. Same keur-eval benchmark + acceptance
gate throughout — the benchmark is the stable foundation while the detector
implementation evolves.

## The one rule to never break

`detectors → evidence → deterministic policy → ALLOW/WARN/BLOCK`.
AI may add evidence. AI may never be the referee.
