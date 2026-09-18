# ADR-009 — Labeled corpus: an oracle-free evaluation loop

**Status:** Accepted
**Date:** 2026-09-18
**Builds on:** ADR-004 (analysis engine evolution), the tool-oracle corpus
(`eval/corpus/`, `eval/buildgt.sh`) and the keur-eval extender (`eval/extend/`).

---

## Context

keur trains its rule set against a **ground truth** — the set of findings a
piece of code *should* produce. Today that ground truth is built one way:

**Tool-oracle corpus (`eval/corpus/`).** Clone ~21 real, security-relevant
repos; run proven tools (semgrep, gitleaks, trivy, trufflehog, checkov) over
them; treat their union as truth (currently 683 findings). The learning loop
(`optimise.sh`) then grows keur's rules to approach that baseline.

This is valuable — it tests generalisation to real, messy code — but it has a
structural limit: **the oracle is a proxy for truth, not truth.**

- If keur flags something the tools missed, the corpus scores it a *false
  positive* — even when keur is right. Precision is measured against the tools'
  blind spots, not against reality.
- The tools' own false positives get baked in as things keur is trained to
  reproduce.
- There is no clean signal for the **true negative**: code that is deliberately
  safe and must stay quiet. Absence of a tool finding is not the same as a
  labeled "this is correctly not a finding".

The question this ADR settles: can we have a ground truth where we *know* the
label — TP / FP / FN / TN — without running an oracle tool at all, and drive a
rule-building loop off it?

## Decision

**Yes. We adopt a second, complementary evaluation loop built on a *labeled*
corpus, where the expected verdict is declared in the fixture itself and no
oracle tool is consulted.** It does not replace the tool-oracle corpus; the two
measure different things and both are kept.

### The label is the ground truth

Each case is a small, **fictional** code fragment that carries its own expected
verdict as an inline annotation:

```text
# KEUR-EXPECT: KEUR-SEC-006      <- this line/file MUST produce this rule  (TP)
# KEUR-EXPECT-NOT: KEUR-SEC-006  <- this rule MUST NOT fire here           (TN)
```

- A **positive** fixture with `KEUR-EXPECT: R` is a true-positive probe: rule
  `R` must fire. If it does not → **false negative**.
- A **negative** fixture with `KEUR-EXPECT-NOT: R` is a true-negative probe:
  `R` must stay silent. If it fires → **false positive**.
- A **regression** fixture pins a specific past bug so it cannot return.

Because the verdict is declared, **TP / FP / FN / TN are computed directly** by
scanning the fixture and comparing fired rules to the annotation — deterministic,
fast, and oracle-free. This is the same discipline semgrep uses to test its own
rules (`ruleid:` / `ok:` annotations); keur applies it with its own engine.

### The loop

```text
concept (CWE / OWASP / GHSA / semgrep-registry / cpm — metadata only)
   │
   ▼  gen-from-knowledge.sh  (local LLM writes a fresh fictional TP + TN pair
   │                          and a candidate rule; --mock runs without a model)
   ▼  selfcheck-gate.sh      (scan each fixture; positive must fire, negative
   │                          must stay quiet → TP/FP/FN measured against labels)
   ▼  pass → review-queue/                       fail → self-correct (back to LLM)
   ▼  promote.sh  (human + Kiro validate)  ← OUTSIDE the loop, never auto-commit
   ▼  keur-eval corpus  (positive/ | negative/ | regression/)
```

The generation + gate steps are the automatic loop. Promotion into the corpus
is a deliberate human step: nothing is auto-committed.

### Boundaries (why fictional-only)

The labeled corpus is **fictional-only**. Sources provide *concepts* (a CWE
weakness, an OWASP category, a GHSA advisory shape); the generator writes new
fictional code from them. We never copy real code, secrets, or PII, and never
scrape proprietary dashboards (Sonar, CodeRabbit, …). This keeps the corpus
licence-clean and safe to commit, and it is what lets us *own* every label.

### Two loops, two jobs

| | Tool-oracle corpus | Labeled corpus (this ADR) |
|---|---|---|
| Truth source | proven tools' output (proxy) | declared label in the fixture (exact) |
| Code | real repos (messy, realistic) | fictional fragments (clean, targeted) |
| Measures | generalisation to real code | correctness against a definition |
| True negatives | weak (absence of a tool hit) | first-class (`KEUR-EXPECT-NOT`) |
| Precision signal | biased by the tools' blind spots | exact |
| Cost | clone repos + run 5 tools | scan a fixture, no oracle |
| Oracle needed | yes (semgrep/gitleaks/…) | **no** |

A rule should earn its place in **both**: pass the labeled gate (it is correct
by definition and does not fire on the safe variant) *and* not wreck precision
on the real-code corpus (it generalises). Passing only the labeled gate risks a
rule that is perfect on tidy fixtures and brittle on real code; passing only the
tool corpus risks training toward the tools' mistakes. The two are guardrails
for each other.

## Consequences

- **A rule's acceptance is provable from committed artifacts.** The fixture +
  its annotation are in `keur-eval`; anyone can re-run the gate and reproduce
  the TP/FP/FN verdict without cloning repos or installing scanners.
- **Precision becomes honestly measurable** via true-negative fixtures — the
  thing the tool-oracle corpus cannot give us.
- **The loop stays human-gated at promotion.** Auto-generation and auto-grading
  are cheap and safe; auto-committing generated rules/fixtures is not, so it is
  deliberately excluded. A future "auto-promote on gate-pass + generalisation
  check" mode is possible but is a separate decision.
- **Two corpora to maintain.** Accepted as the cost of measuring both
  generalisation and definitional correctness.
- The tool-oracle corpus and its train/holdout split are unchanged; this ADR
  adds a loop, it does not remove one.

## Alternatives considered

- **Hand-label the real-repo corpus instead of using tools.** Rejected:
  labeling thousands of files by hand does not scale and is exactly what the
  oracle tools automate. The labeled corpus gets exact labels by keeping cases
  small and fictional, not by hand-auditing real code.
- **Drop the tool-oracle corpus once labeled fixtures exist.** Rejected: fictional
  fixtures do not exercise the messiness of real code; generalisation would go
  unmeasured. Keep both.
- **Auto-commit gate-passing drafts (fully closed loop).** Deferred: a model can
  write a fixture that happens to fit its own regex, so an independent (human)
  check at promotion is retained for now. Revisit behind an explicit flag.
