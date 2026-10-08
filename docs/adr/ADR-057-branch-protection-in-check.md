# ADR-057 — Branch protection in `keur check`: warn/block direct commits to the default branch

@id ADR-057
**Status**: accepted · **Date**: 2026-10-08 · **Coverage**: code (`git.current_branch`/`default_branch`, `branch_protection_finding` in check.cpp) + tests · **Relates to**: ADR-003 (git adapter — the one git touch point), ADR-026 (maturity vs action), ADR-035 (self-explaining findings & suppression), ADR-039 (ility alignment), ADR-052/054 (file-level/config checks are repo questions, same spirit)

## Context

Committing straight to the default branch (`main`/`master`) skips review — no
MR/PR, no second pair of eyes, no CI gate before the change is on the line
everyone builds from. A guardrail tool should notice. keur did **not**: `keur
check` read the staged files but never the branch, so there was nothing to flag
a direct commit to `main` (this ADR's author committed 14 times to `main` in one
session with no signal — the gap that prompted this).

This is **not a `.rule`.** The rule engine scans file *content* (ADR-001); the
current branch is git *state*, not a line in a file. So the check belongs in the
`keur check` flow, which already talks to git through the adapter (ADR-003).

Two things it must respect:
- **Maturity (ADR-026).** A prototype commits to `main` all day; a managed
  service must not. So the strictness scales: a nudge at low maturity, a block at
  high — exactly the KEUR-CFG-001/002 shape (ADR-054).
- **Turn-off-ability.** A solo repo, or a trunk-based team, legitimately commits
  to the default branch. The check must be suppressible like any rule, not a
  hardcoded veto.

## Decision

Add branch awareness to the git adapter and a maturity-gated finding to the
check flow.

@id ADR-057.adapter

- `Git::current_branch()` — `rev-parse --abbrev-ref HEAD`, "" when detached.
- `Git::default_branch()` — the last segment of `symbolic-ref
  refs/remotes/origin/HEAD` when a remote HEAD is known, else
  `config init.defaultBranch`, else `"main"`. Best-effort name, read-only — it
  only ever drives a warn/block, never mutates anything.

@id ADR-057.finding

- `branch_protection_finding(current, default, level)` is a **pure** function
  (testable without git): returns a `Finding` only when `current == default` and
  `level >= 2`; action is `warn` at level 2-3, `block` at level >= 4; nullopt
  otherwise. The level is the resolved **modifiability** ility (ADR-039 —
  branch discipline is a process/maintainability concern), read from the same
  config stack `keur difficulty` uses.
- check.cpp emits it as a normal `scan::Finding` with rule id **KEUR-BRANCH-001**
  right after the scanners, BEFORE the suppression pass — so `[rules] disabled`,
  `keur:ignore KEUR-BRANCH-001`, and the keurignore file all silence it exactly
  like any rule (ADR-035). The verdict is still the one pure policy path
  (ADR-014): the finding feeds `policy_evaluate` like every other.

## Why these thresholds

Warn from 2 (developing) / block from 4 (managed) mirrors KEUR-CFG (ADR-054), so
the two "process hygiene" gates escalate together and a team's maturity dial
moves them in step. Level 1 (prototype) stays silent: a throwaway repo should not
nag about branches.

## Consequences

**Positive**
- A direct commit to the default branch is visible (warn) and, at managed+,
  stopped (block) — the review workflow is enforced by the same gate that scans
  the diff.
- Suppressible per repo (trunk-based teams opt out with one `[rules] disabled`
  line) and off entirely below maturity 2.
- Branch logic lives behind the git adapter (ADR-003); the decision is a pure,
  unit-tested function.

**Negative / risk**
- `default_branch()` is heuristic without a remote (`init.defaultBranch`/`main`).
  Acceptable: it only picks a name to compare against for a warn/block, and a
  wrong guess at worst mis-nudges — never mutates. A repo whose default is
  neither can suppress or set `init.defaultBranch`.
- keur now blocks its own direct commits to `main` at level 5 (confirmed). That
  is the intended behaviour; day-to-day work moves to feature branches + MRs. A
  deliberate exception uses `keur:ignore KEUR-BRANCH-001` or `--no-verify`.

## Alternatives considered

- **A `.rule`.** Rejected: the engine has no view of git state; a rule would be a
  lie about what it scans (ADR-001/040).
- **A hardcoded block on main/master.** Rejected: not maturity-aware, not
  suppressible — it would fail trunk-based and solo workflows and could not be
  dialed down.
- **A separate `pre-commit` shell check.** Rejected: duplicates the verdict/
  suppression machinery check already owns; a second gate path is exactly what
  ADR-014 forbids.

## Open questions

- Should `pre-push` protect additional branches (release/*), not just the
  default? Likely a later, config-driven allowlist of protected globs.
- Make the protected-branch set configurable (`[branch] protected = [...]`) once
  more than the default branch needs guarding.
