# KEUR — Architecture

> The deterministic gate. Works fully without AI. AI is an optional escalation layer.

## Pipeline

```text
                         git diff (staged / range)
                                   │
                          ┌────────▼────────┐
                          │  File / type    │  respect .gitignore, tier scanners,
                          │  filter         │  path excludes, size caps
                          └────────┬────────┘
              ┌───────────┬────────┼────────┬───────────┐
              ▼           ▼        ▼        ▼           ▼
           secrets      deps     sast      pii     custom-rules
          (gitleaks)  (trivy) (semgrep) (keur-pii) (keur-rules)
              │           │        │        │           │
              └───────────┴────────┼────────┴───────────┘
                                   ▼
                        ┌──────────────────────┐
                        │  Evidence aggregator  │  normalise → Finding,
                        │  + fingerprint        │  dedupe by fingerprint,
                        │  + confidence calib.  │  corroboration bonus
                        └──────────┬───────────┘
                       ┌───────────┴───────────┐
                       ▼                       ▼
                    certain                ambiguous band
                       │                       │
                       │                       ▼
                       │                     Ollama (structured JSON)
                       │                       │
                       └───────────┬───────────┘
                                   ▼
                        ┌──────────────────────┐
                        │     Policy engine     │  deterministic rules over
                        └──────────┬───────────┘  calibrated evidence
                                   ▼
                          PASS  /  WARN  /  BLOCK
```

## The Finding (normalised evidence interface)

Every scanner is adapted into one shape. This makes scanners interchangeable.

```json
{
  "id": "uuid",
  "source": "gitleaks|trivy|semgrep|keur-pii|keur-rules|sonar|ollama",
  "rule": "RULE-ID",
  "category": "secret|dependency|sast|pii|quality",
  "file": "src/payment.ts",
  "line": 42,
  "severity": "critical|high|medium|low",
  "message": "…",
  "confidence": 0.91,
  "deterministic": true,
  "fingerprint": "sha256(source+rule+file+normalised-snippet)",
  "evidence": ["gitleaks: aws-access-key", "corroborated: semgrep S1234"]
}
```

**Findings are evidence, not truth.** A finding never directly equals a verdict.

## Confidence calibration

Base confidence comes from the scanner config. It is then adjusted:

```text
confidence = clamp(
    base_confidence
  + corroboration_bonus * (independent_agreeing_sources - 1)
  - ambiguity_penalty,
    0, 1)
```

Later, calibration can be *learned* from the corpus so that "confidence 0.93" means
"≈93% of similar findings were correct" (see the evaluator design). Until then, the
configured base values are used verbatim and documented.

## Policy engine (deterministic)

Given the calibrated findings for a diff:

1. Any finding in `block-categories` (e.g. `secret`) with a blocking source → **BLOCK**.
2. Else if max calibrated confidence ≥ `block-at` on a blocking-action scanner → **BLOCK**.
3. Else if max calibrated confidence ≥ `warn-at` → **WARN** (prompt / annotate).
4. Else → **PASS** (still recorded).

The AI layer may *add* evidence (a finding with `source=ollama`), which flows through
the same policy. It cannot short-circuit the decision.

## Tiers

| Tier | Budget | Scanners | AI |
|------|--------|----------|----|
| pre-commit | ~3s | secrets, pii, custom-rules | off |
| pre-push | ~30s | + deps, sast | optional |
| CI | unlimited | + cross-file | on |

Rationale: a pre-commit hook must be fast or people disable it. Heavy/semantic work
moves down the line where latency is acceptable.

## Reuse from cpm (attribution)

Patterns adopted from `github.com/rkristelijn/cpm`: two-mode hooks, phased
autofix→block→warn execution, tiered gates, `.rule` file format, findings-as-evidence,
JUnit/SARIF output, persistent findings history. KEUR narrows these to hook management

+ AI-boundary determinism rather than general project maturity.
