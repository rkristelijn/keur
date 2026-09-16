# KEUR

**Deterministic guardrails for AI-generated code.**

KEUR verifies code before it reaches your repository. It runs a set of
deterministic scanners, normalises their findings into one evidence model, and a
policy engine decides **ALLOW / WARN / BLOCK**. A local LLM may be used as an
optional escalation layer for uncertain cases — but the policy engine, never the
LLM, is the referee.

> Status: early / experimental. The design is settled; the implementation is
> being built incrementally. See `docs/design/`.

## Why

Half of code-quality and security work is the same shape: *input comes in →
classify what it is → gather context → decide → sometimes escalate*, with a
privacy boundary. KEUR makes that shape composable and deterministic, so it can
run on a git hook (pre-commit / pre-push) or as a filter in front of an AI.

The core rule, never broken:

```
detectors → evidence → deterministic policy → ALLOW / WARN / BLOCK
```

AI may add evidence. AI may **not** be the referee. That separation is what makes
KEUR trustworthy enough to gate a commit.

## How it fits together

```
        git diff
           │
           ▼
   ┌───────────────┐
   │  scanners     │  gitleaks · trivy · semgrep · PII/CBI · custom rules
   └──────┬────────┘
          ▼
   ┌───────────────┐
   │ evidence      │  one Finding model + fingerprint + per-source confidence
   │ normalisation │
   └──────┬────────┘
          ▼
   ┌───────────────┐        uncertain?
   │ policy engine │◄───────  optional local LLM (escalation only)
   └──────┬────────┘
          ▼
   ALLOW / WARN / BLOCK
```

Tiered by speed:
- **pre-commit** — fast deterministic checks (<~3s)
- **pre-push** — fuller analysis (<~30s)
- **CI** — full repo, cross-file, optional LLM, benchmark regression

## Repositories

- **keur** (this repo) — the product: scanners, evidence model, policy engine,
  hooks, CLI, optional LLM escalation.
- **keur-eval** — the immutable evaluation corpus (ground truth): fictional,
  annotated test cases KEUR is scored against. See `docs/HOWTO-keur-eval.md`.

## Design docs

- `docs/design/keur-design-rationale.md` — architecture, principles, staged plan.
- `docs/design/keur-eval-convention.md` — the evaluation-corpus contract.
- `docs/design/lessons-from-cpm.md` — what to carry in / avoid, and the
  ISO/IEC 25010 quality attributes mapped to KEUR.

## Principles (short)

1. Findings are **evidence, not truth**.
2. The **benchmark decides** whether the detector improved — not the LLM.
3. **Config as data** — detection rules live in data files, not code.
4. KEUR **works without AI**; the LLM is an optional accelerator.
5. Optimise **classification quality** (precision/recall), not finding count.

## Layout

```
scanners/   tool adapters (gitleaks, trivy, semgrep, …)
lib/        Finding model, fingerprinting, evidence normalisation
policy/     deterministic decision engine
rules/      KEUR detection rules (data)
optimiser/  LLM hypothesis generator + acceptance gate (later)
eval/       harness that runs KEUR (and reference tools) vs keur-eval
hooks/      pre-commit / pre-push / CI entry points
bin/        keur scan | check | diff | fix | hook | ci
docs/       design + how-to
```

## Status of the pieces

| Piece | State |
|---|---|
| Design + conventions | ✅ documented |
| keur-eval corpus + generator | 🟡 generating (draft cases, needs review) |
| Scanner adapters / Finding model | ⬜ next |
| Policy engine | ⬜ next |
| Evaluator (score vs keur-eval) | ⬜ next |
| Optimiser loop | ⬜ only after evaluator + corpus are proven |

## License

See `LICENSE`.
