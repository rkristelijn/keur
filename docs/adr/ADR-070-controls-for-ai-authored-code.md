# ADR-070 — Controls for fully AI-authored code: the independent-referee model

@id ADR-070

## Status

Proposed · 2026-11-04 · **Coverage**: none
· Relates to: ADR-001 (native RE2 engine — the deterministic referee), ADR-009
(rules are data, the engine stays simple — new detection is a `.rule`, not engine
logic), ADR-004 (staged path to semantic analysis — the layer that catches
logic-not-pattern slop), ADR-056 (orchestrate clang-tidy semantic checks — the
parked semantic gate this ADR argues to promote), ADR-032 (trustworthy ground
truth — the oracle this ADR says must have a human-signed slice), ADR-031
(independent validation), ADR-023 (synthetic corpus generation — the thing a
human golden-set must sit beside, not replace), the `train` acceptance gate
(README / `eval/optimise.sh` — "the benchmark decides, never the LLM"), the
`dogfood` + self-check CI jobs (`.github/workflows/ci.yml`), ADR-035
(self-explaining findings), ADR-066 (external scanners opt-in — the
corroboration layer), `make mutate` (the existing, un-gated mutation target)

## Context

Treat this as a thought experiment made concrete: **assume every line of keur —
C++, `.rule` files, ADRs, CI steps, even the ground-truth corpus — was proposed
by an LLM.** The question is not "do we trust the AI" but "which independent,
deterministic controls sit between AI output and what actually runs or ships?"
That is keur's own thesis applied to keur itself: *findings are evidence, the
policy is the referee, and the referee must be independent of the player.*

The empirical case for caution is now strong, not speculative:

- **AI code is insecure at a high base rate.** Pearce et al. (2022) found ~40%
  of Copilot programs contained vulnerabilities (≈50% in C, ≈39% in Python). A
  2026 formal-verification study ("Broken by Default", arXiv:2604.05292) found
  **55.8% of AI-generated artifacts carried at least one vulnerability**, with
  1,055 formally proven via a Z3 SMT solver; the best model still graded D. A
  large GitHub-corpus study (arXiv:2510.26103) is the optimistic bound and still
  leaves ~12% with CWE-mapped flaws.
- **Iterative AI refinement makes it *worse*, not better** — the finding most
  relevant to keur. Shukla et al. (arXiv:2506.11022) ran 400 samples through
  LLM-only "improvement" loops and measured a **37.6% increase in critical
  vulnerabilities after just five iterations**, accumulating non-linearly
  (early iterations ≈2.1 vuln/sample, late iterations ≈6.2). Crucially, *even
  prompts that explicitly asked for security* introduced new flaws (cryptographic
  misuse, over-engineering, outdated patterns), and complexity growth predicted
  vulnerability count (r = 0.64). Their headline mitigation: **cap LLM-only
  iterations (they propose ≤3) and force a human/deterministic review between
  them.** keur's own `train` loop and any coding-agent session *are* exactly
  such a loop.
- **The supply chain has an AI-specific hole: slopsquatting.** LLMs hallucinate
  package names that do not exist; a USENIX Security '25 study measured
  hallucination rates of **5.2% (commercial) to 21.7% (open-source models)**,
  and prompt-adversarial "slopsquat baits" push local models to 40–73%
  (arXiv:2608.23897). Attackers register the phantom names. A bad-known-list
  rule cannot catch a *newly invented* name — this needs an existence check, not
  a blocklist. CSA calls it "an active supply chain risk requiring immediate
  action."
- **The governance consensus: controls must fire regardless of the model's
  agreement, and must fire *before* generation lands, not after.** OWASP's
  "Secure Coding with AI" / AISVS Appendix-C and the NIST AI RMF
  (Govern/Map/Measure/Manage) both frame guardrails as *external, deterministic
  gates* in the existing SSDLC. Policy applied only *after* code is generated
  "creates a false sense of control" — the agent may already have fetched an
  unapproved package, altered a lockfile, or weakened a test in the same change.

keur already embodies much of this. What it does **not** yet have is a named,
written model of *which control covers which AI failure mode, at which moment,
and where the human/deterministic independence actually lives.* Without that,
the gaps are invisible — and a gap you cannot see is slop you will ship.

## Decision

Adopt the **independent-referee model** as keur's explicit stance on AI-authored
code, and write it down as a control catalogue. The governing invariant:

> For every class of AI failure, at least one control must be **independent of
> the generating model** and **deterministic or human** — never the same LLM
> that produced the artifact, grading its own work.

keur is **the referee layer in a human's SSDLC**, not an autonomous replacement
for review. Each failure class below maps to the check-moment that owns it and
the control that makes the referee independent. "keur is what this is" — so each
row that is a pattern becomes a `.rule` (ADR-009), each row that is a gate
becomes a CI/hook step, and each row that needs semantics escalates the parked
ADR-004 / ADR-056 layer rather than being faked in regex.

### Control catalogue (failure class → moment → control → independence)

| # | AI failure class | Evidence | Check moment | Control | What makes it independent |
|---|---|---|---|---|---|
| C1 | Insecure-by-pattern code (hardcoded secret, weak crypto, injection sink) | 40–55.8% base vuln rate | pre-commit / pre-push | RE2 rule corpus (`security/`, `secrets/`) | Deterministic engine, same verdict every run (ADR-001) |
| C2 | Logic-correct-looking but semantically wrong (auth always-true, off-by-one, broken bounds) | "subtle flaws", Z3-proven bugs | pre-push / CI | Semantic layer: clang-tidy orchestration (ADR-056), path to dataflow (ADR-004) | A different analysis engine, not pattern-matching |
| C3 | Iterative degradation — vulns accumulate across LLM-only loops | +37.6% crit @ 5 iters | `train` loop **and** agent sessions | Iteration cap + mandatory deterministic/human gate between rounds | The benchmark + a human, not the LLM self-assessing |
| C4 | Slopsquatting — hallucinated / phantom dependency | 5.2–21.7% hallucination | pre-commit / CI | Dependency **existence** check against the registry, not a blocklist | The registry is ground truth external to the model |
| C5 | Weak tests that co-evolve with the bug (test + code from one LLM) | "false sense of security" | scheduled | Mutation testing (`make mutate`, mull — nightly `mutation.yml`, not PR-CI) | Injected faults the AI never saw must be caught |
| C6 | Un-reviewed *decisions* — an ADR that elegantly justifies a bad call | governance consensus | PR review | Human sign-off on ADRs; index-sync is necessary but not sufficient | A human, by definition |
| C7 | Gate bypass — `--no-verify`, agent edits lockfile/CI in same change | "the whole change… what the agent ran" | branch protection | Server-side required checks; `dogfood` re-runs the gate on the merged diff | GitHub-enforced, cannot be skipped client-side |
| C8 | Oracle capture — AI grades itself against an AI-written ground truth | ADR-023 is synthetic | `train` acceptance | Human-signed golden slice as a veto beside the synthetic corpus (ADR-032) | The slice is never touched by the generating model |

The two controls already *correct* and load-bearing today — keep and defend
them: the `train` acceptance gate ("recall rises, precision ≥ floor, regex
generalises, **the benchmark decides, never the LLM**") is the live
implementation of C3+C8 for rules; the `dogfood` CI job (keur gates its own PR
diff) is the live implementation of C7 for code. This ADR's job is to name the
*gaps* — C2, C4, C5, C6, C8-for-code — and the order to close them.

### Priority ordering (cheapest-defensible first)

1. **C5 — mutation testing as a standalone scheduled workflow** (advisory
   first, like `deb-lint` in
   ADR-069, then blocking). The machinery exists; it only needs wiring. This is
   the single cheapest win and directly answers "AI-written tests prove AI
   assumptions, not truth."
2. **C4 — a dependency-existence rule/adapter.** A `supply-chain` check that
   resolves each imported package against the registry and flags the
   *non-existent* ones (the slopsquatting signature), distinct from the existing
   known-bad lists.
3. **C8-for-code — a human-signed golden corpus slice** beside the synthetic
   one (ADR-032 / ADR-023), used as a veto in `eval/` so a metric can never rise
   on synthetic-only agreement.
4. **C2 — promote ADR-056 / ADR-004 semantic layer from parked to opt-in-then-
   default.** The largest gap for AI-only code, and the one regex structurally
   cannot cover.
5. **C6 / C7 — process controls:** require PR review for ADRs, document that
   `main` must have server-side branch protection so `--no-verify` is never the
   last gate (C7's independence lives outside the repo, so it must be *stated*
   here even though it cannot be asserted in-tree).

### What this is NOT

- **Not a claim that keur alone makes AI-authored code safe.** It makes the
  *referee* independent; a human still owns the decision (C6). The research is
  unanimous that human-in-the-loop is the primary defense (C3).
- **Not a rewrite of the engine.** Every pattern control is a `.rule` (ADR-009);
  only C2 needs a genuinely different analysis engine, and that is ADR-004/056,
  not new logic smuggled into RE2.
- **Not blocking on cosmetic tools.** New gates start advisory and are promoted
  once their output is triaged (the ADR-069 discipline).

## Consequences

- **Positive**: keur gains a written threat-model for its own premise. Every
  control traces to a measured AI failure mode and an independence guarantee, so
  a reviewer (human or AI) can see *which* slop each gate stops and which gaps
  remain open.
- **Positive**: the gaps become a backlog, not a hidden risk. C4 and C5 in
  particular are concrete, cheap, and close the two most AI-specific holes
  (slopsquatting, self-confirming tests).
- **Positive**: it hardens keur's own `train` loop against the exact
  degradation Shukla et al. measured — the iteration cap + benchmark veto is now
  a stated requirement (C3/C8), not an emergent property.
- **Cost**: C2 (semantic layer) is real engineering and deliberately sequenced
  last; until it lands, logic-not-pattern slop remains the known residual risk,
  and this ADR says so plainly rather than implying coverage it lacks.
- **Named residual risk (honesty, per AGENTS.md "be honest about limits")**:
  C6/C7 independence lives *outside* the repository (human reviewers, GitHub
  branch protection). keur cannot assert it in-tree; it can only refuse to be
  the last line of defense and document the requirement.
- **Neutral**: no default behaviour changes on adoption. This ADR is the map;
  the follow-ups are the territory.

## Follow-ups

1. **C5** (done): added a standalone `.github/workflows/mutation.yml` —
   mull-based mutation testing on a nightly `schedule` + `workflow_dispatch`,
   deliberately *out* of `ci.yml` (ADR-041: slow + toolchain-fragile, keep the
   PR loop fast). Advisory: it reports the mutation score + surviving mutants;
   promote to a required check once the surviving-mutant list is triaged.
2. **C4**: author a `supply-chain` dependency-existence check (rule + adapter)
   that flags imports absent from the registry; add true/false-positive fixtures
   (a real package vs. a plausible phantom).
3. **C8**: carve a human-signed golden slice out of `eval/corpus/`, wire it as a
   veto in the acceptance gate so no metric improves on synthetic agreement
   alone (extends ADR-032).
4. **C3**: make the `train` loop's LLM-only iteration cap explicit and
   configurable (default ≤3, per arXiv:2506.11022), resetting after each
   benchmark pass.
5. **C2**: schedule the ADR-056 clang-tidy orchestration from parked → opt-in,
   measure its precision on the corpus, then consider default-on.
6. **C6/C7**: document in `CONTRIBUTING.md` that ADRs require human review and
   that `main` carries server-side required checks + branch protection.
