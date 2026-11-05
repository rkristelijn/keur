# ADR-073 — Hermetic e2e smoke for context-dependent rules; commit-message markers deferred

@id ADR-073

## Status

Accepted · 2026-11-05 · Coverage: tests/e2e/smoke.sh
· Relates to: ADR-057 (branch protection / KEUR-BRANCH-001 — the rule whose
context-dependence surfaced this bug), ADR-068 (merge-aware scoping — the change
that re-ordered branch protection before the empty-scope early return), ADR-026
(maturity levels — why keur's own repo blocks at level 5), ADR-063 (derive
KEUR_HOME from the binary — why a hermetic `env -i` run still resolves rules),
ADR-041 (tiered CI — the smoke test is the cheap post-build gate), ADR-035
(self-explaining — the rule's own message pointed at the fix)

## Context

After ADR-068 (#28) merged to `main`, CI went red: the `build` jobs on Linux and
macOS failed at the post-compile e2e smoke step (`tests/e2e/smoke.sh`), while
Windows, the unit/integration suites (275+68 tests), compilation, and every
`scan`-based check stayed green. The failing assertion was test 5,
`check (no stage)`:

```text
FAIL  check (no stage) (exit=1, want=0)
  BLOCK KEUR-BRANCH-001 | block | conf 0.9 | main |
        Committing directly to the default branch 'main'. …
  KEUR (pre-commit): BLOCK -- blocking finding(s) above; commit stopped
```

**Root cause** (confirmed by diffing `check.cpp` across the v0.2.0→main range and
reproducing locally): ADR-068 deliberately moved branch protection
(KEUR-BRANCH-001, ADR-057) to run **before** the empty-scope early return. The
pure helper `early_pass_on_empty_scope(files_empty, has_branch_finding)` now
returns PASS only when `files_empty && !has_branch_finding`. That ordering is
**correct product behaviour**: a merge whose resolved scope is empty is still a
direct commit to a branch, and branch discipline is a property of the *branch*,
not of the staged file set.

The problem is not the engine — it is **where the smoke test ran the binary**.
keur is a **local git commit/push hook, not a CI gate.** Several of its rules are
properties of the git context a committer sits in. KEUR-BRANCH-001 is the clearest
example: on keur's own repo — held to maturity level 5 (ADR-026), where branch
protection escalates from WARN to BLOCK — a direct commit to the default branch
`main` is correctly blocked. CI's `actions/checkout` puts the working tree on a
real `main` branch, and the smoke test ran `keur check` *from the repo root*. So
the harness subjected the binary to keur's own on-`main` governance and got a
BLOCK, i.e. exit 1, where it expected PASS / exit 0.

This did not reproduce on the maintainer's machine for two independent reasons:
a detached-HEAD worktree makes `current_branch` empty (branch protection returns
`nullopt`), and a global keur config under the user's XDG config dir masks the
repo-level maturity. Both are
accidents of the local environment — exactly the kind of hidden coupling a smoke
test should not carry.

The `check (no stage)` assertion is meant to prove a **binary property**: "with
nothing staged, `keur check` prints PASS and exits 0." That property must hold
independent of which branch a CI runner happens to check out and independent of
any global config on the runner.

## Decision

**Run the `check (no stage)` smoke assertion in a hermetic sandbox** — a throwaway
git repo on a **feature branch**, with `HOME` and `XDG_CONFIG_HOME` isolated to a
scratch directory via `env -i`:

```sh
SB="$(make_sandbox)"   # mktemp -d; git init -b feature/smoke-check; local identity
check "check (no stage)" 0 "PASS" -- \
  env -i PATH="$PATH" HOME="$SB/home" XDG_CONFIG_HOME="$SB/home/.config" \
  sh -c 'cd "$1" && exec "$2" check' _ "$SB" "$BIN"
```

A `trap … EXIT` removes the sandbox. The isolation is belt-and-suspenders:

- **Feature branch** → `current_branch != default_branch` → KEUR-BRANCH-001 does
  not fire at all, which is the branch a real user is actually on when the "clean
  tree passes" property matters.
- **No keur.toml in the sandbox + isolated HOME/XDG** → maturity resolves to the
  default (level 1), so no level-gated rule escalates, and neither keur's own
  level-5 config nor the runner's global config leaks in.

keur still resolves its rules correctly under `env -i` because `KEUR_HOME` is
derived from the binary location (ADR-063), so wiping the environment does not
break rule discovery. The other four assertions (`version`, `help`,
`policy explain`, `scan`) are environment-independent and stay as plain
repo-root invocations.

This is a **fix-forward in the test**, not a change to the engine: ADR-068's
ordering stays, KEUR-BRANCH-001 keeps blocking a real developer who sits on
`main`, and the smoke test goes back to asserting the binary property it was
always meant to assert.

### Alternatives considered

**(1) Commit-message markers to steer blocking — DEFERRED.**
Let a commit message carry an explicit marker (e.g. `keur-allow: KEUR-BRANCH-001`
or a `[keur-skip]` tag) that `keur check` reads to down-grade or suppress a
finding for that one commit, so a deliberate direct-to-`main` commit (release
bumps, CI bots) can pass on intent rather than by disabling the rule everywhere.

This is an attractive *product* feature but the wrong tool for *this* bug, and it
is deferred for concrete reasons:

- **It does not fix the smoke test.** The smoke test never makes a commit — it
  runs `keur check` against a clean tree. There is no commit message to carry a
  marker, so a marker channel would leave test 5 red.
- **Timing.** In a pre-commit hook, `keur check` runs *before* the commit message
  is finalised for several flows (e.g. `git commit` with the editor still open),
  so the message is not reliably available at check time. A pre-push tier sees
  commit messages, but branch protection is a pre-commit concern.
- **Scope / blast radius.** Adding a message-driven override channel changes the
  verdict contract (`findings → deterministic policy → PASS/WARN/BLOCK`) by adding
  a second, out-of-band input. That is a real design change deserving its own
  decision, its own tests, and a careful anti-abuse story (a marker must not
  become a universal `--no-verify` that silently defeats the gate keur exists to
  be). It should not be smuggled in as a CI build-fix.
- **Existing escape hatch.** A committer who genuinely must commit to the default
  branch already has `keur:ignore KEUR-BRANCH-001` and `[rules] disabled`
  (ADR-035). The marker idea's value is *ergonomics* (per-commit, intent-scoped),
  not a missing capability.

If pursued later, the shape worth exploring is a **pre-push-tier** marker that is
(a) explicit and visible in history, (b) scoped to a single rule id and a single
commit, and (c) logged in the finding stream so an override is auditable, never
silent. Tracked as a follow-up, not part of this change.

**(2) Spoof keur's keur.toml (lower the maturity level in CI) — REJECTED.**
Setting a lower `modifiability` for the CI run drops KEUR-BRANCH-001 from BLOCK to
WARN (or off). It works mechanically but it is dishonest — it makes keur's own
repo pretend to be less mature than it is — and it only masks *this* rule; the
next level-gated rule that fires in CI would need the same hack. It also couples
the test to a magic config value. Rejected in favour of true isolation.

## Consequences

- **Positive**: `main` goes green; the v0.3.0 release is unblocked. The smoke test
  asserts the binary property it intends to, with no hidden dependence on the
  runner's branch or global config.
- **Positive**: the pattern generalises — any future smoke assertion that invokes
  a context-dependent rule (anything keyed on branch, maturity, or global config)
  can reuse `make_sandbox` for a hermetic run.
- **Positive**: ADR-068's correct ordering and KEUR-BRANCH-001's real-developer
  behaviour are both preserved; nothing in the engine changed.
- **Cost**: the smoke test now shells out to `git init` + `env -i` for one
  assertion. Negligible, and it runs on Linux, macOS, and MSYS2/Windows (plain
  POSIX `sh -c` + `mktemp -d`).
- **Neutral**: the commit-message-marker capability remains unbuilt. Users who
  must commit to the default branch continue to use `keur:ignore` / `[rules]
  disabled` until/unless a follow-up ADR adopts markers.

## Follow-ups

1. If per-commit, intent-scoped overrides prove worth the complexity, open a
   dedicated ADR for a **pre-push-tier, auditable** commit-message marker
   (alternative 1), with explicit anti-abuse constraints — never a silent blanket
   bypass.
2. Audit the remaining smoke assertions for any other hidden environment coupling
   and move them into the sandbox if found.
