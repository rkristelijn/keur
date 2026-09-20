# ADR-018 — cpm as a second quality gate over keur (parked)

@id ADR-018
**Status**: proposed (parked) · **Date**: 2026-09-20 · **Coverage**: none · **Relates to**: ADR-001 (keur's engine is a clean-room of cpm's design), ADR-012 (pipeline coverage), ADR-017 (surfacing metrics)

<!-- Coverage: none — decision recorded, not yet built. Parked: the value is
     confirmed but the corpus-exclude must be solved before cpm check is usable
     as a gate over keur's own code. -->

## Context

cpm (github.com/rkristelijn/cpm) is a more mature sibling with the same anti-slop
goal and its own quality tooling (cppcheck, clang-tidy, complexity, comment-ratio,
secrets, inclusivity, markdown/yaml lint) that keur does not have natively. keur's
rule engine is already a clean-room of cpm's design (ADR-001) and the `.rule`
format is cpm-compatible.

Ran `cpm check` (0.9.0) over keur to see if it adds value:

- **cpm's own mature checks find ZERO findings on keur's own source** — nothing
  in `src/`, `docs/`, `eval/`. keur's code is clean by cpm's bar.
- One legitimate nudge: `KEUR-INCL-002: dumb → use simple` on the ADR-009/AGENTS
  "engine stays dumb" wording.
- cpm runs keur's own rules (shared format) but does not yet know the `multiline`
  and `count` engines → "unsupported engine" warnings (expected; those are newer
  in keur than in cpm).
- **Noise problem:** cpm scanned `eval/corpus/repos/` too (1375+ findings), even
  though that path is gitignored — the vulnerable fixtures dominate the output.

## Decision (to make)

Wire cpm in as a **second, independent quality gate** over keur's OWN code:

1. Add a `cpm.toml` at the keur root (project metadata + `[checks]` + `[limits]`,
   modelled on cpm's own cpm.toml).
2. **Exclude the corpus** (`eval/corpus/repos/`) and `rules-imported/` from cpm's
   scan — OPEN QUESTION: the exact cpm exclude mechanism was not confirmed
   (cpm scanned the corpus despite .gitignore). Must be determined first
   (config key, scan-root, or a cpm-side fix) or the gate is unusable (drowned
   in fixture findings).
3. Optionally add a CI job running `cpm check` (non-blocking first, then gating)
   once the exclude works.

## Why parked

- The value is real (independent mature checks, and keur already passes them —
  good regression gate) but ADDITIVE: keur's own pre-commit hook + Sonar +
  CodeRabbit already cover the current PR. This is not urgent.
- The corpus-exclude is an unsolved prerequisite; shipping cpm without it just
  produces 1375 fixture findings and no signal.
- Adding a fourth tool to the gate (keur hook, Sonar, CodeRabbit, cpm) is an
  architecture decision worth recording, not a drive-by change on a green PR.

## Consequences

- Until decided: cpm can be run ad hoc (`cpm check`) but is not a wired gate.
- When picked: a `cpm.toml` + confirmed corpus-exclude + (optional) a CI job.
  Feeding cpm's `multiline`/`count` support back upstream would remove the
  "unsupported engine" warnings and keep the two engines in step.
