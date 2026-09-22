# ADR-023 — Synthetic corpus generation in the learning loop

@id ADR-023
**Status**: proposed *(direction to discuss; no code beyond the existing gen-cases.sh)* · **Date**: 2026-09-21 · **Coverage**: none *(decision recorded before building)* · **Relates to**: `docs/design/learning-loop.md` (the tool-baseline loop this extends), `docs/design/evaluator.md`, ADR-001 (RE2 engine — the thing being trained), ADR-018 (cpm as a second gate), ADR-019 (external scanners — reused here as verifiers), and the existing `eval/gen-cases.sh` + `eval/perturb.sh`.

## Context

The learning loop (`docs/design/learning-loop.md`) trains keur's rules to
reproduce a **proven-tool ground truth** over **real cloned repos**. Measured on
the current corpus, recall plateaus around 0.77–0.79 and the misses cluster by
category: **PII (~104), SECRET (~88), CBI (~80), CMDI (~20), SQLI (~20)**. Nearly
all rejects are `holdout-fail` (~2517) or `overfit-regex` (~2467), not
`no-valid-rule` (17). So the engine is expressive enough; the limiter is the
**training signal**: the loop only learns patterns that (a) appear in the cloned
repos and (b) the proven tools happen to flag there.

That caps learning on categories the real repos under-represent. You cannot
learn a PII rule from repos that contain little PII, no matter how good the
model is. Cloning more repos helps holdout diversity but is a blunt instrument:
you cannot *aim* it at a specific missed category.

A second signal already half-exists: **`eval/gen-cases.sh`** has a local model
(Ollama) *write* a fictional snippet for a category and prepend a `KEUR-EXPECT:
<rule-ids>` annotation — a case whose ground truth is declared by the generator.
`eval/perturb.sh` then produces a form-changed variant to catch rules that
learned layout instead of concept. But this is a manual, human-reviewed drafting
tool; it is **not wired into the loop**, and its ground truth is *self-declared*.

## The trap this ADR must avoid

If **one model** both writes the case *and* proposes the rule, the loop is a
closed self-confirming circle: the model invents a case it can trivially match,
writes a regex that matches exactly that, and "passes" — learning nothing that
transfers to real code. This is classic self-training collapse. Any use of
generated cases as training signal MUST break this circle.

## Decision (proposed)

Add **synthetic corpus generation as a SECOND, aimable training signal**, gated
by an **independent verifier** so the generator can never grade its own work.

The generate→verify pipeline for one synthetic case:

1. **Generate (LLM).** Ask the local model for a short, fictional snippet in a
   target category (reuse `gen-cases.sh`), with a declared `KEUR-EXPECT`.
2. **Verify independently (the circle-breaker).** Run the **proven tools**
   (gitleaks / semgrep / trivy / presidio / …, the ADR-019 scanners) over the
   generated snippet.
   - If a proven tool independently flags the expected class → the case is
     **admitted** to the corpus with the tool's finding as its ground truth (two
     independent sources agree; the annotation is corroborated, not trusted).
   - If no tool flags it → **discard** the case (the model imagined something no
     real detector recognises — exactly the noise we must not train on).
3. **Perturb (anti-form).** Keep only cases whose finding survives
   `perturb.sh` reformatting, so admitted cases test concept, not layout.
4. **Split.** Admitted synthetic cases join train/holdout like cloned repos, so
   a rule learned on synthetic data must still generalise to *held-out* cases
   (synthetic or real) — the same gate (`docs/design/learning-loop.md` §gate).

The invariant, restated: **the generator proposes candidates; a proven tool (not
the generating model) certifies ground truth.** Same shape as the rule gate
(benchmark decides, not the LLM) — applied one level earlier, to the data.

### What the generator is allowed to be

The generator and the rule-proposer MAY be the same Ollama model — the circle is
broken by the *tool verifier* in step 2, not by using two models. (Using a
different model for generation is a cheap extra safeguard, not the primary one.)

### Where synthetic data helps most

Aim it at the **under-represented misses**: PII, SECRET, CBI. These are hard to
find in general OSS but easy to generate fictionally and easy for a proven tool
(gitleaks for secrets, presidio for PII) to verify. This turns "clone and hope"
into "generate the exact gap, verified."

### Stack-targeted generation: the "best repo ever" and "worst repo ever"

Beyond single categories, generation can be **aimed at a whole stack** (e.g.
NestJS, Django, Spring, Rails). For a target stack, ask the model for two
matched sets:

- **N antipatterns → the "worst repo ever" (positives).** Known bad patterns for
  that stack: SQL built by string concat in TypeORM, missing `@UseGuards`,
  secrets committed in `.env`, unvalidated DTOs, `eval`/`child_process` on
  request data, etc. These test **recall**.
- **N best practices → the "best repo ever" (negatives).** Idiomatic, correct
  code for the same stack: parameterised queries, guards/validation pipes,
  secrets from a vault/config service, etc. These test **precision** — keur must
  stay silent on good code. This is the half the current corpus is thinnest on
  (it explains most of the 2517 holdout-fails), so best-practice negatives are
  as valuable as antipattern positives.

Why a *stack* is a good unit: the model has strong, well-documented priors for a
named stack, and the two sets are naturally paired (the best-practice version of
each antipattern), which sharpens the recall/precision boundary a rule must
respect.

**Same admission rule applies, with one honest caveat.** Every generated case —
best-practice or antipattern — still passes step 2 (proven-tool verification):
an antipattern a tool won't flag is discarded; a "best practice" that trips a
tool is not a clean negative and is discarded. BUT many stack antipatterns are
**architecture/quality**, not security ("business logic in the controller",
"missing guard") — no scanner verifies those. For those, the tool-verifier path
does not apply; they need the human-review path (`gen-cases.sh --review`) or
must be treated as keur-only patterns (open question #3). Do not silently admit
quality patterns as if a tool certified them.

## Why not the alternatives

- **Only clone more real repos** (status quo + ADR-019 corpus growth): helps
  diversity but cannot target a specific missed category; PII/secret density in
  general OSS is low. Keep doing it — it is complementary, not a substitute.
- **Trust the generator's `KEUR-EXPECT` as ground truth** (skip step 2): this is
  the self-confirming collapse. Rejected outright — it would train keur on the
  model's imagination.
- **Human-label every generated case**: correct but does not scale; that is what
  `gen-cases.sh --review` already offers for occasional use. The tool-verifier
  is the scalable substitute for the human where a proven tool covers the class.
- **Fine-tune the model instead**: violates the single-binary / deterministic
  posture (ADR-002) and produces a black box; keur's output must stay a readable
  `.rule` (ADR-001).

## Consequences

- The loop gains a **second, aimable signal** for exactly the categories real
  repos under-represent — directly attacking the recall plateau.
- Ground truth stays **objective**: certified by a proven tool, never by the
  generating model. No self-confirmation.
- Reuses what exists: `gen-cases.sh` (generate), the ADR-019 scanners (verify),
  `perturb.sh` (anti-form), the existing gate + split (generalisation). The new
  code is a **pipeline that wires these together**, plus a discard log.
- Risk: a category with **no proven-tool verifier** (e.g. a keur-only CBI
  pattern) cannot be certified this way; those still need human review or a real
  fixture. Document per-category which path applies.
- Cost: generation + running the full tool battery per synthetic case is heavier
  than one measure pass; run it as a supervised background phase (like
  `supervise.sh`), not inline per rule iteration.

## Open questions (settle before building)

1. **Volume + ratio.** How many synthetic cases per real repo before synthetic
   noise dominates? Cap the synthetic fraction of the corpus?
2. **Verifier confidence.** Admit on any tool hit, or require a minimum
   confidence / two-tool corroboration (reuse the policy corroboration-bonus)?
3. **Category coverage.** Which missed categories have a proven-tool verifier
   (PII→presidio, SECRET→gitleaks) vs. which are keur-only (CBI) and need a
   different admission path?
4. **Provenance.** Tag admitted cases as `source:synthetic` in the ground truth
   so measurement can report real-vs-synthetic recall separately (don't let
   synthetic wins mask a real-world plateau).
