# ADR-071 — Grounded consistency models: a WARN-only corroboration layer, never a gate

@id ADR-071

## Status

Proposed · 2026-11-04 · **Coverage**: none
· Relates to: ADR-070 (controls for AI-authored code — this ADR answers its open
question "can a model judge text?" for the semantic/doc classes C2/C6/C8),
ADR-001 (deterministic RE2 engine — the invariant this must not break),
ADR-066 (external scanners opt-in — the corroboration slot this fits), ADR-010 /
ADR-047 (annotrace — the deterministic claim-checking this complements),
`eval/doc-coverage.sh` + ADR-061 (doc chapter contract — the deterministic doc
checks already present), the `train` loop (already runs a local Ollama model —
same local-only pattern), ADR-035 (self-explaining findings)

## Context

A recurring, reasonable question: *can't a small model just read text
(markdown, an ADR, a commit message) and tell us whether it is nonsense?* The
honest answer requires separating three things a "small model" could do, because
only one of them is safe to adopt and the other two quietly break keur's thesis
(ADR-070: the referee must be independent of, and more reliable than, the
generator).

**Route 1 — LLM-as-judge ("understand this, is it nonsense?").** Tempting and
wrong for a gate. The judge is then the same class of system that produced the
slop. The iterative-degradation study (arXiv:2506.11022) showed a model asked
*explicitly* to improve security introduced new flaws and failed to recognise
its own errors. An LLM grading truth hallucinates a confident "this is fine"
over nonsense just as readily as the reverse. It *feels* like understanding; it
is not an independent, deterministic control.

**Route 2 — embedding / semantic distance ("does this resemble known-good
text?").** More deterministic (same text → same vector) and already in reach
(the knowledge tool uses MiniLM search). But it measures **similarity, not
truth**: "the sky is green" and "the sky is blue" sit next to each other in
embedding space. Useful for *deviation from a norm* ("this ADR looks unlike any
other"), useless for *nonsense-as-falsehood*.

**Route 3 — grounded consistency ("is claim A supported by source B?").** This
is what the small models people point to actually do, and it matters that we are
precise about it. Vectara's **HHEM** (Hughes Hallucination Evaluation Model, a
few-hundred-MB model, 100k+ downloads, runs on CPU) scores 0.0–1.0 for whether a
summary is *"factually consistent based on search results"* — i.e. against a
**provided source**. Siblings (**Bespoke-Minicheck**, **Lynx**, **FAVA**) are
the same family: reference-grounded consistency checkers. They are **not**
absolute truth oracles — without a source document they have nothing to measure
against.

Two facts bound what we can claim for Route 3:

1. **They need a source.** "Is this ADR's conclusion consistent with the
   research it cites?" — yes, given the research as the source. "Is this lone
   claim true?" — no, there is no referent.
2. **They are probabilistic, not deterministic, and imperfect.** Survey work
   (arXiv:2505.04847, arXiv:2508.08285) finds current detectors struggle on
   longer documents and that the benchmarks themselves (often ROUGE-based) align
   poorly with human judgment. The same input can cross a threshold differently
   after a model update.

So Route 3 is essentially ADR-070's "verifiable claim" idea made *softer*: it
checks a claim against a *supplied* source instead of an exact link/id — gaining
reach over prose, losing determinism.

## Decision

**Permit a grounded consistency model (HHEM-class) as an opt-in, local,
WARN-only corroboration layer — never a BLOCK gate, and only ever with an
explicit source.** Three hard constraints, each a direct consequence of the
context:

1. **WARN, never BLOCK.** A probabilistic, model-based signal must not produce a
   verdict that aborts a commit. Breaking "same input → same verdict" (ADR-001)
   would make the referee an unreliable oracle — exactly what ADR-070 forbids.
   Its output maps to WARN at most: a nudge that routes a human to review
   (ADR-070 control C6). The deterministic rules remain the only source of
   BLOCK.
2. **Always grounded — no source, no check.** The model is only ever asked "is
   this claim consistent with *that* source?", never "is this true?". Concretely:
   an ADR body vs. the research/ADRs it cites; a doc section vs. the chapter it
   summarises (ADR-061); a PR description vs. its own diff. If there is no
   explicit source to bind to, the check does not run.
3. **Local by default + opt-in (ADR-066 slot).** It runs on-device by default (the
   `train` loop uses the local Ollama endpoint unless `OLLAMA_URL` is set; a remote
   `OLLAMA_URL` sends the prompts off the machine) and is
   off by default. It is corroboration a user turns on, not a hidden dependency,
   consistent with "keur's own engine is the product; everything else is an
   opt-in corroboration layer."

This keeps the division of labour clean and honest:

- **Deterministic claim-checking** (dead links, `@implements` to a non-existent
  id, a phantom dependency — ADR-010/047, `doc-coverage.sh`, ADR-070 C4) stays
  the hard gate. It can BLOCK because it is deterministic and the referent is
  external and exact.
- **Grounded consistency** (HHEM-class) is a *fuzzier, WARN-only* extension over
  prose, for claims whose referent is a *document* rather than an exact id.
- **LLM-as-judge (Route 1)** is **rejected** for any control role. It may assist
  a human interactively, but it never emits a keur verdict.
- **Embeddings (Route 2)** are allowed only as WARN-level anomaly nudges
  ("unlike any other ADR"), never as a truth signal.

### What this is NOT

- **Not a truth detector.** It detects *claim-vs-source inconsistency*, a
  narrower and honest thing. "Nonsense that happens to be internally consistent
  with its cited source" still passes — only a human (C6) catches that.
- **Not a new default.** Nothing changes on adoption; this reserves a slot and,
  decisively, *forbids* the unsafe wiring (BLOCK, ungrounded, or LLM-as-judge)
  that someone would otherwise be tempted to add later.
- **Not a replacement for the deterministic doc checks.** It sits beside
  `doc-coverage.sh` and annotrace, not on top of them.

## Consequences

- **Positive**: the "can a model spot nonsense?" question now has a written,
  defensible answer. The one safe use (grounded, local, WARN-only) is permitted;
  the two unsafe uses (LLM-as-judge BLOCK, ungrounded truth claims) are
  explicitly ruled out before anyone builds them.
- **Positive**: it extends keur's reach over *prose* (ADRs, docs, PR text) —
  where RE2 and exact-id checks cannot go — without compromising the
  deterministic core.
- **Positive**: reuses the existing local-model pattern (Ollama in `train`) and
  the opt-in corroboration model (ADR-066); no new always-on dependency.
- **Cost / named risk (honesty, AGENTS.md "be honest about limits")**: the model
  is imperfect and non-deterministic. Mitigated structurally by confining it to
  WARN — a false WARN costs a glance, never a blocked commit. A false *negative*
  (misses an inconsistency) is no worse than today, where there is no check at
  all.
- **Neutral**: determinism is preserved where it matters. Every BLOCK still
  comes from a deterministic rule; the model can only ever add a WARN.

## Follow-ups

1. Prototype an opt-in `doc-consistency` corroboration check: feed (claim, cited
   source) pairs from an ADR/doc to a local HHEM-class model, emit a WARN below a
   configurable consistency score. Measure its precision on keur's own ADRs
   before suggesting it to anyone.
2. Define the (claim, source) extraction for the first target — likely ADR body
   vs. its `Relates to:` / cited ADRs, reusing annotrace link resolution.
3. Document in `config/keur.toml` as a commented, off-by-default corroboration
   entry (the ADR-066 pattern), with the WARN-only + grounded constraints stated
   inline so the limits travel with the config.
