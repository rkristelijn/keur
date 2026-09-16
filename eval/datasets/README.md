# eval/datasets — corpus splits

The self-improving loop uses four splits. **The split boundary is a hard rule.**

| Split | Used by | Ollama may see it? |
|-------|---------|--------------------|
| `train/` | candidate generation | yes |
| `validation/` | accept/reject decisions | metrics only |
| `evaluation/` | periodic held-out check | **NO — never** |
| `regression/` | must-never-break cases | metrics only |

## The one rule that matters most

The optimiser must **never** see or modify `evaluation/`. If it does, it will learn to
pass its own tests (overfitting) and KEUR will look better on paper while getting worse
on unseen code. The held-out evaluation corpus is the only honest measure of
generalisation.

Each case is stored with ground-truth labels (expected TP/FP/FN per file/line) so the
evaluator can compute precision/recall/regression-safety deterministically.

See `docs/design/evaluator.md`.
