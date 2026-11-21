# ADR-072 — Orphan / unreferenced file detection: the reachability WARN

@id ADR-072

## Status

Proposed · 2026-11-04 · **Coverage**: none
· Relates to: ADR-070 (controls for AI-authored code — this is a new control for
the "leftover scratch / abandoned file" failure mode AI agents and humans both
cause), ADR-010 / ADR-022 (annotrace + cross-reference engine — this is the same
link graph, walked in the opposite direction), `eval/doc-coverage.sh` (dead-link
checker — the forward edge; this ADR adds the inbound-edge check), ADR-061 (doc
chapter contract), ADR-071 (grounded consistency — the *content* question this
ADR deliberately does NOT try to answer), ADR-035 (self-explaining findings),
ADR-046 / `.keurignore` (the allowlist an intentional standalone file uses)

## Context

A distinct kind of "nonsense" keur does not yet catch: not bad *content*, but a
file that should not still be there — leftover scratch output, a half-finished
analysis, a doc that was written and then orphaned when the thing it described
moved or was renamed. AI coding agents are prolific producers of these: each
session can drop a `notes.md`, a `why-X-vs-Y.md`, a one-off `*.out.txt`, and
nothing cleans them up. Over time the repo accumulates files that *look* real
(they are tracked, they have plausible content) but that **nothing references** —
dead weight that misleads readers and rots.

A concrete, measured example drove this ADR. A reachability scan of
`keur-sonar/docs/` (114 tracked `.md`) found **11 files with zero inbound
references** — ~10%:

```text
docs/demo/why-keur-vs-other-tools.md      (an essay stranded in a demo-tape dir)
docs/HOWTO-keur-eval.md
docs/design/authored-ground-truth-format.md
docs/design/composite-migration.md
docs/design/difficulty-resolution.md
docs/design/doc-standards-by-cmmi.md
docs/design/learn-loop-bottlenecks.md
docs/design/proven-baseline.md
docs/design/scanner-dispatch-plan.md
docs/rules/KEUR-CMDI-003.md
docs/rules/KEUR-XSS-002.md
```

The trigger case (`why-keur-vs-other-tools.md`) is instructive: its *content* is
fine, but it sits in `docs/demo/` (a directory for `.tape`/`.gif` demo
artifacts) and **nothing links to it**. It is not wrong — it is *orphaned and
misplaced*. That is exactly the detectable kind of rubbish, because "what
references this?" has an exact answer: nothing.

Three kinds of "junk file" get conflated; only two are deterministically
catchable, and this ADR is precise about which:

1. **Untracked scratch** — never committed (`scratch.py`, a stray `*.out.txt`,
   `.tmp/`). 100% deterministic from `git status`: tracked? no → candidate. keur
   already has `.keurignore` + the `.tmp/` convention for the expected ones.
2. **Orphaned / unreferenced tracked files** — committed, plausible, but **no
   inbound reference** from any root. This is the measured case above, and the
   focus of this ADR.
3. **Content nonsense** — "is what this file *says* sensible?" Undecidable in
   general (ADR-071); only the grounded WARN layer touches it. **Out of scope
   here.**

## Decision

Add an **orphan-file reachability check** as a deterministic, **WARN-only**
control. It is the inbound-edge mirror of `doc-coverage.sh`'s dead-link
(outbound-edge) check, over the same reference graph (ADR-022):

1. **Build the reference graph.** Nodes = tracked files (scoped initially to
   `docs/**` + assets). Edges = every reference found in the repo: markdown
   links `](path)`, `href=`, `@see`/`see path.md`, backtick-quoted `*.md`, and
   annotrace `@implements`/`@proves` ids (ADR-010).
2. **Walk from roots.** Roots are the always-reachable anchors: `README.md`,
   `CONTRIBUTING.md`, `AGENTS.md`, `CHANGELOG.md`, the ADR index
   (`docs/adr/README.md`), `docs/design/vision.md`. Anything a root can reach,
   transitively, is referenced.
3. **A tracked file with no inbound edge and not reachable from a root is an
   orphan candidate → WARN**, with a self-explaining message (ADR-035): "no
   inbound reference — leftover scratch, or link it from where it belongs."

Hard constraints, each a direct consequence of the context:

- **WARN, never BLOCK.** An orphan can be *intentional* — a standalone reference
  doc, a template, a file linked only from outside the repo. A probabilistic-ish
  "nobody links this" signal must not abort a commit. It routes a human to
  decide: delete, or wire it in (ADR-070 control C6).
- **Opt-out via the existing allowlist.** A deliberately-standalone file is
  marked in `.keurignore` (ADR-046) — the same allowlist mechanism, no new
  concept. That turns "intentional orphan" into an explicit, reviewable choice
  rather than recurring noise.
- **Conservative edge extraction.** The dead-link half already parses these
  reference forms; reuse it. Under-linking (missing an exotic reference form)
  produces a false-positive WARN, which is cheap — a glance — and is *why* this
  is WARN, not BLOCK. The measured 11 above are *candidates*, not proven dead
  weight.

### What this is NOT

- **Not a content judge.** It answers "is anything pointing here?", never "is
  this any good?". A well-referenced file full of nonsense still passes (that is
  ADR-071's WARN territory); an orphaned-but-excellent file still WARNs until
  linked or allowlisted.
- **Not an auto-deleter.** keur never removes a file. It surfaces the candidate;
  the human (or a follow-up task) decides.
- **Not a new graph engine.** It reuses the cross-reference graph (ADR-022) and
  the link parsing already in `doc-coverage.sh`; this is a second traversal
  direction, not new machinery.

## Consequences

- **Positive**: the "leftover file" failure mode — a prime AI-agent rubbish
  source — becomes a visible, deterministic signal instead of silent rot. The
  measured 11/114 shows it is a real, recurring ~10% problem, not hypothetical.
- **Positive**: closes the loop with `doc-coverage.sh`. keur already checks that
  every *link* has a target (no dead links); this adds that every *doc* has a
  linker (no orphans). Together they keep the doc graph fully connected.
- **Positive**: reuses existing mechanisms end to end — the reference graph
  (ADR-022), the link parser (`doc-coverage.sh`), and the allowlist
  (`.keurignore`, ADR-046). No new engine, no new config concept.
- **Cost / named risk (honesty, AGENTS.md "be honest about limits")**: edge
  extraction is heuristic — a reference form it does not parse yields a false
  orphan. Confined to WARN, the cost is a glance; over time the parser gains the
  missing forms as real false positives are triaged. A false *negative* (a
  genuinely orphaned file it thinks is linked) is no worse than today.
- **Neutral**: scoped to `docs/**` + assets first. Extending to source files
  (an unreferenced `.cpp`/`.rule`) is possible later but noisier — compilers and
  the rule loader already surface truly-unused code, so docs are the high-signal
  starting point.

## Follow-ups

1. Add an inbound-edge traversal to `eval/doc-coverage.sh` (it already parses
   the outbound links): from the roots, mark reachable; report unreachable
   tracked `docs/**` files as orphan WARNs.
2. Seed `.keurignore` with the intentional standalones among the measured 11
   (e.g. `docs/rules/KEUR-*.md` if they are meant to be id-addressed, not
   linked), so the first run's WARNs are all genuine leftovers.
3. Triage the remaining measured orphans with Remi: delete the true scratch
   (the stranded `docs/demo/why-keur-vs-other-tools.md` essay → move or remove),
   link the keepers from where they belong.
4. Optionally add an untracked-scratch WARN (kind #1): tracked? no, and not in
   `.gitignore`/`.keurignore`, and older than N days → "uncommitted leftover".
