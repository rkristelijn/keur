# KEUR

**Deterministic guardrails for AI-generated code — a configurable git-hook gate.**

> Dutch *keuren* = to inspect, to approve/reject. KEUR decides whether code may pass:
> **PASS / WARN / BLOCK**.

KEUR manages configurable git `pre-commit` and `pre-push` hooks — globally and per
repo — that run a pipeline of deterministic scanners (secrets, deps, SAST, PII, custom
rules) and turn their findings into a reproducible policy decision. AI is an optional
*escalation layer*, never the referee.

## Why KEUR exists (and how it relates to cpm)

`cpm` (github.com/rkristelijn/cpm) is the **laboratory**: benchmarks, corpus,
rule-authoring, experiments — "how well do we detect problems?" KEUR is the
**product**: "may this code pass?" — the CLI, the hooks, the finding engine, the
policy engine, the AI integration, the SARIF/JSON output.

KEUR reuses cpm's strongest patterns:

- **Two hook modes** — per-repo (`.git/hooks/`) and global (`~/.config/git/hooks/`).
- **Phased execution** — autofix → block → warn → commit-msg.
- **Tiered gates** — fast `pre-commit` (<3s), fuller `pre-push` (<30s), unlimited CI.
- **Declarative `.rule` files** — configurable, per language/domain.
- **Findings as evidence, not truth** — normalise, fingerprint, calibrate confidence.
- **Persistent history** — every run/iteration is recorded for trend + regression.

## Two core ideas

### 1. Deterministic gate (the product)

```text
git diff → file filter → [gitleaks · trivy · semgrep · pii · custom rules]
        → evidence aggregator (fingerprint + confidence)
        → certain? → POLICY        uncertain? → OLLAMA (structured result)
        → POLICY ENGINE → PASS / WARN / BLOCK
```

- **You cannot make AI deterministic. You can make the AI boundary deterministic.**
  Ollama may say "this looks like SQL injection". Ollama may **not** decide "therefore
  the pipeline passed". The policy engine decides. That is what makes it reproducible.
- External tools (gitleaks, trivy, semgrep, sonar, coderabbit, copilot) are
  **evidence sources**, not absolute ground truth. Corroboration raises confidence;
  agreement is not proof (tools share blind spots).

### 2. Self-improving evaluator (the laboratory loop)

An optimisation loop where existing tools are *noisy oracles*, a curated corpus is
*ground truth*, and Ollama is a **local meta-optimiser** that proposes the smallest
possible change. The **benchmark decides** whether it is an improvement — never the LLM.

- Optimise for **classification quality** (precision/recall/regression-safety), never
  "number of findings".
- **Immutable evaluation set** — the optimiser may never see or modify the final
  evaluation corpus (prevents overfitting / learning-to-pass-its-own-tests).
- **Acceptance gate** with hard constraints; Pareto tracking (recall +x%, precision
  −y%, critical-FN −z) instead of collapsing everything into one number too early.

See `docs/design/` for the full architecture.

## Status

Early scaffold. Deterministic gate first (works fully without AI); the optimiser and
the CPM-Eval bridge come after the gate is solid.

## Quick start (planned CLI)

```bash
keur hook --install            # per-repo hooks
keur hook --global             # global hooks (all repos)
keur scan [path]               # run the deterministic pipeline
keur check                     # gate the current staged diff (used by hooks)
keur check --ai=ollama         # opt in to the AI escalation layer
keur policy explain            # show the active policy + thresholds
```

## Design principles (non-negotiable)

1. Works fully **without AI**. AI is `--ai` opt-in, default off on pre-commit.
2. **Findings are evidence**; the policy engine makes the decision.
3. **Reproducible**: same input + same config + same rule versions → same verdict.
4. **Configurable everywhere**: global → repo → directory override hierarchy.
5. **Fast by tier**: pre-commit stays sub-second-ish; heavy analysis moves to push/CI.
6. **No hardcoded paths**; portable across macOS and Linux/WSL.
