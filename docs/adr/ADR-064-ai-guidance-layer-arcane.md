# ADR-064 — Pair keur's gate with an AI-guidance layer (claude-code-arcane): prevent, don't only block

@id ADR-064

## Status

**Status**: Proposed · **Date**: 2026-10-25 · **Coverage**: none
· Relates to: ADR-045 (go-public distribution — a new channel sits behind the
same public-first gate), ADR-062 (CI distribution Action/component — sibling
channel; this is the "into the AI's workspace" channel), ADR-011 (project
profile — keur's profile axis maps onto arcane's profile composition), ADR-010
(annotrace — the vocabulary an AI must follow when it writes), the README's
"More than a hook → a copilot for coding and vibe-coding" positioning, and the
scope line "define KEUR scope — guard not coach"
· External: [claude-code-arcane](https://github.com/SebastianLuser/claude-code-arcane)
(MIT) — an `npx` configuration harness that installs skills/agents/hooks/rules
into a project's `.claude/` by profile.

## Context

keur is a **deterministic verification layer**: staged files → RE2 engine →
findings → policy → PASS/WARN/BLOCK. It acts *after* code exists. Its whole
value is that it is dumb, fast, and reproducible — it does not reason, it checks
(ADR-002, ADR-009).

That leaves a gap keur deliberately does not fill: **the code should not have
been written that way in the first place.** A BLOCK is a late signal — the AI
(or human) already spent the effort, and now has to backtrack. The README
already names the ambition beyond the hook ("a copilot for coding and
vibe-coding"), but keur must not grow a reasoning brain to get there — that
breaks "guard, not coach" and the single-binary/no-runtime-plugins line.

`claude-code-arcane` is the missing half, already built. It is an `npx` harness
that deploys, per profile, the **AI-facing guidance** a project needs: skills,
agents, rules, and hooks written into `.claude/` so the coding agent is *steered
up front* — the conventions, the security do's/don'ts, the commit/PR discipline.
It is the *generative/preventive* layer where keur is the *verification* layer.
Crucially it is MIT-licensed and explicitly composable (profiles combine with
`+`, and it already leaves room for complementary tools rather than bundling
everything).

Two layers, one direction (shift-left), from opposite ends:

| | Layer | When | Nature | Verb |
|---|-------|------|--------|------|
| **arcane** | AI guidance in `.claude/` | *before* / while writing | generative, heuristic | *prevent* |
| **keur** | pre-commit/CI gate | *after* writing, at commit | deterministic, reproducible | *block* |

The insight driving this ADR: **an agent that was told the rule up front rarely
trips the gate.** Pairing the two means most violations never reach the BLOCK —
the gate becomes the safety net, not the primary teacher. That is a strictly
better developer experience and fewer `--no-verify` reaches (the anti-pattern
ADR-035 is built against).

## Decision

Treat arcane as a **complementary partner layer, integrated by reference — never
vendored or reimplemented.** Concretely:

1. **Offer keur as an arcane skill/profile fragment.** keur ships a small
   `.claude/`-shaped asset (a `keur` skill: "install the deterministic
   pre-commit gate; here is how to read its findings and fix rather than
   bypass") that arcane's `security`/`testing` profiles (or a dedicated
   `+keur`) can pull in. This is a **new distribution channel** alongside the
   Action/component (ADR-062) and brew/npx (ADR-045): keur lands *in the AI's
   workspace*, not just in CI.

2. **Mirror keur rules into arcane guidance (one source, two forms).** A keur
   `.rule` says *what to block*; the matching arcane rule/skill says *what to do
   instead*. These must trace to one owner to avoid the drift class ADR-051/
   ADR-061 warn about: the `.rule` is canonical (it is the enforced truth); the
   arcane guidance **links to the rule id**, it does not restate the regex. A
   future generator could emit the human-facing guidance stub from a rule's
   `message`/`fix` fields (keur rules already carry a self-explaining `fix` —
   ADR-035 — which is exactly the preventive text an agent needs).

3. **Explore a collaboration with the arcane maintainer.** The projects are
   complementary, not competing (arcane even recommends external complements
   rather than absorbing them). A reciprocal listing — arcane lists keur as the
   deterministic-gate complement, keur documents arcane as its guidance layer —
   serves both. Licensing is compatible (both MIT). Any shared asset keeps clear
   per-file provenance, the way arcane's `ATTRIBUTION.md` already does.

### What this is NOT

- **Not vendoring arcane's 453 skills into keur.** keur stays the single
  compiled gate; it does not grow a skills catalog, an agent harness, or an
  `npx` config manager. That is arcane's job and doing it in keur would betray
  "guard, not coach" and the single-binary line (ADR-002).
- **Not making keur depend on arcane at runtime.** The gate must work with zero
  AI tooling present — determinism is the product. arcane is an *optional,
  recommended* companion, like a good linter config: nice to have, never
  required for the gate to gate.
- **Not a reasoning brain in keur.** All "prevent" intelligence lives in the
  arcane layer (heuristic, AI-facing); keur's half stays dumb and deterministic.

### The prerequisite (same as every other channel)

A keur arcane-skill points at the public install (`install.sh` / the Release
assets) exactly as the Action/component and brew/npx do. So this channel sits
behind the **same "repo public first" blocker** as ADR-062/ADR-045: nothing to
reference until keur is public. Sequence it after public + v0.1.1, not before.

## Consequences

- **Positive**: closes the loop the README promises — keur becomes *prevent then
  verify*, not just verify, without keur growing a brain. The gate turns into a
  safety net behind up-front guidance, cutting BLOCK-then-backtrack churn and
  `--no-verify` pressure.
- **Positive**: a new, low-effort distribution channel (keur into `.claude/`)
  that reaches vibe-coders where they already are, reusing the existing
  install.sh/Release artifacts (ADR-045 "distribution is publishing, not new
  builds").
- **Positive**: a concrete collaboration path with an adjacent MIT project —
  reciprocal, not a fork.
- **Risk (named, bounded)**: guidance/rule **drift** — the arcane "do this
  instead" text going stale against the canonical `.rule`. Mitigate by linking
  to the rule id (never restating the pattern) and, ideally, generating the
  guidance stub from the rule's own `fix`/`message` (single source — ADR-051/
  ADR-061). 
- **Cost / open questions**: who owns the generated arcane assets and their
  update cadence; whether a `+keur` profile lives in arcane's repo or keur's;
  the exact handshake with the arcane maintainer. All deferred until after
  public — this ADR records the direction, not the implementation.

## Follow-ups (post-public, do not start before the repo is public)

1. Reach out to the arcane maintainer to confirm interest + the integration
   shape (a `+keur` profile vs. a listed complement).
2. Author the `keur` arcane-skill asset: install the gate, read findings, fix
   over bypass — linking to rule ids, not restating rules.
3. Prototype a generator: keur `.rule` `fix`/`message` → arcane guidance stub,
   so the preventive text stays in sync with the enforced rule (one owner).
4. Reciprocal docs: keur README "guidance layer" note ↔ arcane complement
   listing.
