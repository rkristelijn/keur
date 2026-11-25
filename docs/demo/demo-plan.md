# keur demo plan — new-dev onboarding path

Goal: the demo set should answer a new dev's real questions in order, and each
demo should make clear **why keur beats rolling your own** (a bash hook, a
raw gitleaks/semgrep call, or a one-off script). Every demo = one `.tape`,
AAA pattern, `@shows <rule/ADR id>` marker (ADR-047), rendered to a GIF.

Existing demos (secret-blocking only — the narrowest slice):
- commit-env-file  → SEC-100 / KEUR-SECRET-003
- secret-in-code   → KEUR-SEC-002

Gap: today the demos say "keur blocks secrets". The README claim is "more than
a hook". These proposed demos tell that fuller story.

## Priority list (short, each with the "vs custom" edge)

### 1. install — first contact + supply-chain hygiene
- Arrange: clean machine, no keur. Act: run install.sh, put its bin dir on
  PATH, then run `keur hook --install` in a repo (install.sh installs only the
  binary; it does not touch PATH or install a hook). Assert: checksum verified,
  keur on PATH, the hook stub present in `.git/hooks/pre-commit`.
- Shows: fetch -> SHA256 verify -> hook stub (`exec keur check`).
- vs custom: a hand-rolled hook is a copy-pasted script with no integrity
  check and no versioning. keur ships a verified, versioned binary that
  installs itself as the hook. First thing a security-minded dev wants to see.

### 2. warn-vs-block — the decision model (highest-value)
- Arrange: repo with one advisory/warn finding, then one blocking finding.
  Act: commit each. Assert: WARN proceeds (exit 0), BLOCK stops (exit 1).
- Shows: the PASS/WARN/BLOCK policy (block-at / warn-at, `action`).
- vs custom: a DIY hook is binary (pass/fail). keur has a calibrated
  three-way verdict from confidence thresholds — nag vs gate is a policy
  decision, not hardcoded. This is the mental model everything else needs.

### 3. path-scoped rules — "it won't spam me" (key differentiator)
- Arrange: same rule noisy under ~/git/work/**, wanted in a hobby repo.
  Act: scan both. Assert: suppressed by path in one, active in the other.
- Shows: ADR-043 `[scopes."<glob>".rules] disabled`, opt-out only.
- vs custom: with gitleaks/semgrep you copy a config into every repo or
  maintain per-repo ignore files. keur scopes by directory prefix in ONE
  config, opt-out only (a new repo keeps safe defaults). Answers the #1
  reason devs disable linters: false-positive fatigue.

### 4. custom rule in a client repo — the "special case" story (your ask)
- Arrange: a client repo with a client-specific forbidden pattern (e.g. a
  banned internal hostname, a client's deprecated API call, a naming taboo).
  Write one `.rule` (pattern/presence/absence) under the repo's own rules dir,
  point KEUR_RULES_DIRS at it. Act: commit code that trips it. Assert: flagged
  by the custom rule, with the client's own message/fix text.
- Shows: `.rule` format (cpm-compatible), KEUR_RULES_DIRS, inline
  `keur:ignore <rule-id>` escape hatch.
- vs custom: the alternative is a bespoke grep/regex script living in that one
  client repo that no one maintains and that has no verdict model, no
  suppression, no severity. With keur the client-specific rule is a 5-line
  `.rule` that plugs into the SAME engine, SAME PASS/WARN/BLOCK policy, SAME
  hook — so a special case becomes a first-class, reviewable, suppressible
  rule instead of a snowflake script. This is keur's strongest "beats custom"
  argument: you extend the policy, you don't fork it.

### 5. scan --json — CI / ad-hoc entry point (quick win)
- Act: `keur scan --json src/`. Assert: machine-readable findings.
- vs custom: one tool, one output contract for both the commit hook and CI,
  instead of gluing a different scanner into each.

### 6. policy explain — transparency (quick win)
- Act: `keur policy explain`. Assert: resolved thresholds + verdict mapping.
- vs custom: a DIY hook's policy is buried in shell; keur prints exactly what
  it will block/warn on and where the config came from.

### 7. train / learning loop — "more than a hook" (last; hardest to film)
- Likely `keur train --status` dashboard only (model-driven, hard to make
  deterministic in a tape). Shows the loop that learns rules from your code.
- vs custom: no hand-rolled hook learns. This is the headline differentiator
  but the trickiest to record cleanly; do it once the deterministic ones land.

## Recommended build order
install (1) -> warn-vs-block (2) -> path-scoped (3) -> custom-client-rule (4),
then the two quick wins (5, 6), then train (7). That mirrors the onboarding
journey: get in -> understand the verdict -> trust it won't spam -> extend it
for a client -> wire into CI -> see the policy -> see it learn.

## Determinism notes (so tapes don't rot)
- install: pin the release/version or use a local asset in a hidden Arrange
  step (don't fetch "latest" live).
- all: disposable repo in /tmp, clean neutral shell (same as existing tapes).
- train: status-only unless a seeded, offline fixture can make an accept
  reproducible.
- custom-client rule: ship the example `.rule` in the demo dir so the tape is
  self-contained.
