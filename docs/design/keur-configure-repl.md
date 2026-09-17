# Design: `keur configure` (interactive REPL) — future feature

**Status:** proposed, not implemented
**Raised:** 2026-09-17, during the global-hook / pipeline work

## Problem

Once keur can be installed in several places and modes, a user needs to see and
manage that state without hand-inspecting files:

- Is there a global hook (`core.hooksPath`)? Where does it point?
- Does this repo have per-repo hooks in `.git/hooks`?
- Does this repo have its own `.githooks/` that the global orchestrator chains
  to, and in which mode (augment / replace / keur-only)?
- Is keur wired into this repo's CI pipeline (the non-bypassable gate)?
- Which rule directories are active (`KEUR_HOME/rules`, `KEUR_RULES_DIRS`)?
- Which policy is resolved (block-at / warn-at, from which keur.toml)?

`keur hook --status` already answers part of this (global + repo hook presence),
non-interactively. The gap is a single, guided view + edit surface.

## Proposed shape

`keur configure` opens an interactive REPL that first prints a status board,
then offers guided actions. Non-interactive fallbacks stay available so CI and
scripts never depend on the REPL.

### Status board (read-only, also available as `keur configure --status`)

```
keur configuration
  binary        /usr/local/bin/keur (version 0.1.0)
  global hook   installed -> ~/.config/git/hooks  (core.hooksPath set)
  this repo     /path/to/repo
    per-repo hooks   .git/hooks: (none, global active)
    repo hooks       .githooks/pre-commit: present
    compose mode     augment            (keur.toml [hooks].mode)
    pipeline gate    detected (.gitlab-ci.yml runs 'keur check')  | NOT FOUND
  rules         KEUR_HOME/rules (5), KEUR_RULES_DIRS: (none)
  policy        block-at 0.85, warn-at 0.50  (from ~/.config/keur/keur.toml)
```

### Guided actions (interactive)

- install / remove global hook (wraps `keur hook --global` / `--remove --global`)
- install / remove per-repo hook
- set compose mode for this repo (augment | replace | keur-only) -> writes
  `[hooks] mode` into the repo keur.toml
- scaffold the pipeline gate (emit a `.gitlab-ci.yml` / GitHub Actions snippet
  that runs `keur check`) so the non-bypassable gate is one keystroke away
- show the resolved policy and which file it came from

### Design constraints

- REPL is a convenience layer only. Everything it does must also be doable via
  flags (`keur hook ...`, a future `keur configure set-mode augment`, etc.), so
  automation never needs the REPL. Same principle as the rest of keur: humans
  get ergonomics, machines get deterministic flags.
- No new I/O paths: reuse the git / fs adapters (ADR-003) so the REPL is
  spoofable and testable like the rest.
- Read-only status must be a pure query (safe to run anywhere, no writes).

## Relationship to other work

- Builds on `keur hook --status` (extend it into the status board).
- The "pipeline gate detected" line ties into the CI-gate work: keur runs in the
  pipeline as the non-bypassable poort, the hook is the fast local convenience.
  The REPL should make it obvious when a repo has the hook but NOT the pipeline
  gate (the dangerous "looks covered, is bypassable" state).
- Fits the maturity model's Level 2 (Control) and Level 3 (Visibility): the
  board is the per-repo control/visibility surface a security engineer reads.

## Not now

Interactive REPL work (line editing, menus, terminal handling) is a meaningful
chunk and is deliberately deferred. The immediate need (systemwide coverage) is
met by the global orchestrator hook plus the pipeline gate; this REPL is the
management ergonomics on top, to be built when that base is stable.
