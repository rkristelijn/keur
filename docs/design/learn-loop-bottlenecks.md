# Learn-loop bottleneck analysis (2026-09-22)

A measured diagnosis of where the `keur learn` loop stalls, and where to invest
so it produces enough rules. Every claim below is a measurement from
`eval/history/optimiser.jsonl`, `eval/corpus/groundtruth.jsonl`, and the model
config — not a guess.

## Method

Walk the pipeline schakel-by-schakel; for each, a hypothesis + a measurement +
a verdict. Snapshot taken at 5756 optimiser iterations.

## Results per link

| Link | Measurement | Verdict |
|------|-------------|---------|
| Rules engine (too few primitives?) | 0 `unsupported-engine` warnings; `no-valid-rule` = 0.3% of rejects | **Not a bottleneck** — the engine can express what the model proposes |
| AI / model too limited? | qwen2.5-coder 14B Q5, ctx 32768, temp 0.2; almost always returns a valid rule | **Not a bottleneck** |
| Priming / explanation | SYSTEM prompt lists every engine + generalise instruction | **Not a bottleneck** |
| Context window | 32768 tokens; rules are tiny | **Not a bottleneck** |
| Corpus exhausted? | 19 repos, 683 ground-truth findings, but **0 PII, 0 CBI** in ground truth | **Major bottleneck** |
| Retry / loop | last 500 iters: only **4 unique recall values** (0.462–0.812) | **Bottleneck — stagnation** |
| The gate (overfit/holdout) | **52% holdout-fail + 48% overfit** of all rejects | **Symptom, not cause** |

## The decisive fact

What keur misses vs. what it can learn from:

```text
keur MISSES (FN clusters):     ground-truth HAS (learnable):
  PII    104x                    sast    391
  SECRET  88x                    secret  130
  CBI     80x                    iac     111
  CMDI    20x                    sqli     36
  SQLI    20x                    cmdi     15
                                 PII       0   ← nothing
                                 CBI       0   ← nothing
```

**PII (104) and CBI (80) have ZERO ground truth** — ~60% of the misses are
*unlearnable* with the current corpus. Ground truth comes 100% from 5 tools
(semgrep 470, gitleaks 76, trivy 61, checkov 50, trufflehog 26). **Presidio (the
PII tool) is disabled**, and CBI (internal IP/hostname) is a keur-only category
no proven tool covers.

So the 52% holdout-fails + 48% overfits are largely a *consequence*: the model
strains to invent rules for categories with no reliable signal. The gate is not
too strict — it correctly rejects rules that fit noise.

Where ground truth DOES exist (sast/secret/iac/sqli/cmdi), the loop *does* learn:
corpus recall rose 0.79 → 0.81 overnight with new keepers.

## How to measure each bottleneck (reusable)

1. **Engine coverage:** `grep -c "unsupported engine"` in the log + share of
   `no-valid-rule` rejects. High → engine too limited.
2. **Model quality:** share of `overfit-regex` rejects. High → memorising;
   stronger model or tighter primer.
3. **Corpus exhaustion:** ground-truth findings per category vs. FN clusters. A
   category with misses but 0 ground truth is unlearnable.
4. **Generalisation:** share of `holdout-fail`. High → too little corpus diversity.
5. **Stagnation:** count of unique recall values over the last N iters. Low → the
   loop is chewing the same state.

## Where to invest (by impact)

1. **Close ground-truth coverage (biggest lever, fixes the unlearnable ~60%).**
   - Enable **presidio** → gives the loop PII ground truth (the 104 PII misses).
   - CBI is keur-only → no tool covers it; needs synthetic generation (ADR-023)
     or human `KEUR-EXPECT` fixtures.
2. **Corpus diversity (fixes the 52% holdout-fails).** Clone the already-listed
   clean OSS repos (flask/express/gin/requests) — more variation + better
   negatives.
3. **Synthetic corpus (ADR-023) for PII/CBI/SECRET** — generate the exact gaps,
   tool-verified where a verifier exists.
4. **Stagnation detection in the loop** — after N iters with no unique progress,
   switch category/source instead of burning cycles on the same state.

## What NOT to do

Do **not** add more engine primitives or a bigger model: the data shows neither
is the bottleneck (0.3% no-rule, 0 unsupported-engine). The limiter is the
*training signal*, not expressive power or model capacity. A learn loop produces
enough rules only when every target category has reliable ground truth.
