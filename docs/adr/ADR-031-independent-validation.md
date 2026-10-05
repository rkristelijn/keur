# ADR-031: Independent validation — trusting controls a single author built

@id ADR-031

## Status

Proposed (options laid out; decision open) · 2026-09-26 · Coverage: none
· Relates to: ADR-004 (distillation layer over the field), ADR-023 (synthetic corpus),
ADR-027 (measurement), ADR-030 (corpus revalidation), `keur-design-rationale.md`,
`docs/design/architecture-evolution.md` §6

## Context

keur is, by design and by history, largely a single-author, AI-assisted project:
the code, the rules, and the ground-truth corpus were produced by the same
person and the same class of tools. That is not a criticism — it is the operating
model. But it has one specific structural risk that no amount of internal testing
removes:

> **Correlated failure.** If the author's mental model of "what is dangerous" is
> wrong, it is wrong in three places at once — the code, the rules that detect it,
> and the corpus that measures the rules. An error shared across detector and
> yardstick is invisible to the detector *and* to the yardstick.

keur's own thesis ("the benchmark decides, never the LLM"; ground truth kept
separate from the detector, `keur-design-rationale.md`) defends against the LLM
overfitting to the corpus. It does **not** defend against the *author + corpus*
sharing a blind spot, because the separation is between LLM and benchmark, not
between the author and their own benchmark.

This surfaces as three practical questions the project must answer honestly:

1. Can the author trust controls they built alone?
2. Does keur replace peer review, or is the author reviewing their own work?
3. Is "learn from Sonar/CodeRabbit for the oopsies + good architecture + right
   tools + right questions" enough?

The honest baseline answer (see `architecture-evolution.md` §6): the controls are
currently **consistent and well-structured but not independently validated**, and
keur replaces only the *mechanical* layer of review (secrets, injection, slop) —
never design review, which is out of scope by `scope.md`. Sonar and CodeRabbit
are today the only genuinely *independent* reviewers in the loop, and they are
used mostly as rule *sources*, which is sourcing, not validation.

This ADR records the options for turning independent provenance into a
first-class, permanent control rather than an occasional input.

## Decision

**Independent provenance must be a standing part of the validation story, not a
one-time rule-mining pass.** This ADR does not mandate a single mechanism; it lays
out the options and a recommended combination, because "how much independence is
enough" is a judgement the author should make deliberately.

The principle, stated once: **the defence against a solo blind spot is
deliberately importing other people's.** Every option below is a way to inject
provenance the author did not create, so a shared error has somewhere to be caught.

### Option A — Reference tools run *beside* keur, permanently (recommended core)

Keep gitleaks / semgrep / trivy / gitleaks / Sonar / CodeRabbit running alongside
keur, and treat their **disagreements with keur as the primary signal**, not their
agreements. A finding keur misses that a reference tool catches is a candidate
false-negative *and* a candidate corpus gap (feed it to ground truth, ADR-030).
A finding keur raises that every reference tool misses deserves scrutiny before it
is trusted.

*This is already the ADR-004 "distillation" thesis — this ADR makes it a
validation control, not just a rule-sourcing loop.* The difference: sourcing asks
"what rule can I mine from this tool?"; validation asks "where does the field
disagree with me, and is the field or am I right?"

*Recommended as the core* because it is cheap (the tools already run), it is
genuinely independent (different authors, different training, different blind
spots), and disagreement is exactly the information a solo author cannot generate
alone.

### Option B — Held-out, independently-sourced evaluation slice

Split the corpus so that one slice is built from cases the author did **not**
hand-author: real CVEs, public vulnerable-code datasets, harvested findings from
the reference tools. The optimiser never sees it (ADR-023/027 already require an
immutable eval slice — this strengthens *where the slice comes from*). Precision
on this slice is the number that means "correct," not merely "consistent."

*Why it matters:* a corpus the author wrote measures whether keur matches the
author's model. A corpus sourced from outside measures whether the author's model
matches reality. Only the second breaks correlated failure.

### Option C — Periodic human review of the decisions, not the diffs

Not line-by-line review (that is what keur automates away), but occasional
external human eyes on the two things a gate structurally cannot review:

1. the **ADRs** (are the architectural choices sound?), and
2. the **security-critical rules** (does this rule actually mean what its author
   thinks?).

*Why:* a gate cannot review a design decision or its own premises. Even
infrequent external review of the ADRs breaks the solo correlated-failure risk far
more cheaply than adding more rules. This is the smallest, highest-leverage human
input.

### Option D — Adversarial / mutation testing of the controls themselves

Run mutation testing (the repo already has `mull.yml`) and perturbation
(`eval/perturb.sh`) not just on the engine but on the *rules*: mutate a known-bad
fixture and confirm the rule still fires; mutate a known-good fixture toward
badness and confirm it starts firing at the right point. This tests the control,
not the code — it asks "would this rule survive a variation the author did not
think of?"

*Why:* it is the closest thing to an adversary a solo author can automate. It does
not remove the blind spot, but it widens the set of cases the controls are tested
against beyond what the author explicitly wrote.

### Option E — Do nothing; trust internal consistency (rejected)

Rely on the internal gate alone. *Rejected* — internal consistency is not external
correctness, and this is the exact gap the ADR names. "It passes my own tests
against my own corpus" is faith, not evidence, for a solo project.

### Recommended combination

**A + B as the standing controls, C as the periodic human check, D as the
adversarial floor.** Concretely:

- reference tools run beside keur and disagreements are triaged into ground truth
  (A, continuous);
- the eval slice is sourced from outside the author and never trained against
  (B, structural);
- ADRs and security-critical rules get occasional external human review
  (C, per-release or per-milestone);
- mutation + perturbation run on rules, not only the engine (D, in CI).

## What this ADR explicitly does *not* claim

- It does **not** claim keur replaces peer review. It replaces the mechanical
  layer (secrets/injection/slop, per `scope.md`) and, with A–D in place, does so
  with earned rather than assumed trust. Design review stays a human, out-of-scope
  activity.
- It does **not** claim the controls are trustworthy today. It records what would
  make them trustworthy. Until B ships (held-out, externally-sourced precision),
  "keur passed" means "consistent," not "correct" (see ADR-030 and §6).

## Consequences

**Positive**

- Turns "I learn from Sonar/CodeRabbit" from a one-time sourcing pass into a
  standing independence control — the single most effective mitigation for a solo
  project's correlated-failure risk.
- Gives a defensible answer to "can I trust my own controls?": *as far as an
  independent slice and independent tools corroborate them, yes; beyond that, no —
  and here is the boundary.*
- Reuses machinery that already exists (reference tools, `mull.yml`,
  `eval/perturb.sh`, the immutable eval slice) rather than building new subsystems.

**Negative / cost**

- Reference tools are PATH-dependent and slower; they belong in the `ci` tier / a
  background compare job, not pre-commit (consistent with ADR-028's CI tier).
- Sourcing an external eval slice is real curation work — the deliberate, slow
  path keur chose over cpm's bulk generation.
- Human review (C) requires a second person at least occasionally; a truly solo
  operator gets the weakest form of this control and should be honest about that
  limit.

## Alternatives considered

- **Trust the internal gate alone (E):** rejected — consistency ≠ correctness.
- **Treat reference tools as truth and defer to them:** rejected — they have
  shared blind spots too (agreement ≠ ground truth, per
  `keur-design-rationale.md`); they are *evidence and disagreement sources*, never
  the referee. The policy engine stays the sole referee (ADR-014).
- **Buy correctness with more rules:** rejected — more rules from the same author
  against the same corpus deepens the correlation rather than breaking it. Breadth
  of *provenance* beats volume of rules.

## Open questions

- Which external datasets are license-clean to vendor into the held-out slice
  (note: Sonar's post-2024-11 analyzers are SSALv1 — study, do not copy, per
  `keur-design-rationale.md`).
- The cadence and scope of human ADR review for a solo maintainer, and whether an
  external reviewer can be a periodic contributor rather than a co-maintainer.
- How disagreement-triage (A) feeds ground truth without letting a reference
  tool's false positive pollute the yardstick — the triage itself needs a rule.
