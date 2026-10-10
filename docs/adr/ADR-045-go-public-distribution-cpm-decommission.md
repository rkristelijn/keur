# ADR-045 — Go-public, distribution (brew/npx/apt), and cpm decommission sequence

@id ADR-045

## Status

Proposed · 2026-10-06 · Coverage: none
· Relates to: ADR-028 (sunset cpm; keur is the successor — this sequences it),
ADR-042 (PII-vault scanner — the gate that must pass over ALL history before
public), ADR-043 (path-scoped config — work vs personal rules must not leak),
ADR-026/roadmap (M1+M2 = MVP; M4 = absorb cpm's corpus), ADR-002 (single binary
— what we ship), the Release workflow (`.github/workflows/release.yml`) and
`scripts/release.sh` (how we ship)

## Context

Four entangled questions were raised: when can keur go public, can keur ship to
npx/apt (and brew), and when can cpm be decommissioned (including its apt/npx
channels). They have different answers and a strict order. keur is currently a
PRIVATE GitHub repo; cpm is public and already in maintenance/sunset (ADR-028).
The risk is doing these out of order: going public with PII in history, or
pulling cpm before keur covers what cpm did, both cause real damage.

## Decision

A gated, ordered sequence. Each step has a hard entry criterion; you do not
advance until it is green. No dates — criteria.

```
1. MVP functionally done        (gate: roadmap M1+M2)
2. Pre-public audit passes      (gate: no PII/BII in ANY history; hygiene)
3. keur goes public
4. Distribution: brew → npx → apt   (keur takes the channels)
5. keur absorbs cpm's corpus    (gate: ADR-028 eval-loop adoption, not bulk)
6. cpm decommission             (deprecate → redirect → archive)
```

### Step 1 — MVP done (gate before anything public)

M1 (install that works) + M2 (opt-in proven tools) from the roadmap. Today M1 is
partial, M2 largely open. A public tool that fails its own MVP erodes trust.
`keur doctor` (T-104) just landed; T-105/107 (config-driven gate) and the M2
adapters/suppression are the remaining work. Not public before this.

### Step 2 — Pre-public audit (the hard gate)

A public repo exposes EVERY commit, not just HEAD. So before flipping visibility:

- **PII/BII over full history.** Run the PII-vault scanner (ADR-042) over the
  entire git history, not the worktree — `git log -p` / a history walk, or
  `git filter-repo --analyze`. Today's `VAULT-WORK` catch (a work path in an
  ADR) proves the risk is real and already present. ANY hit = fix history
  (rewrite/squash) before public. This is the single blocking criterion.
- **No private rules leak.** `~/.keur/rules` and any work-specific patterns
  (ADR-042/043) must not be in the repo. Verify none are tracked.
- **Hygiene**: SECURITY.md present (the recurring KEUR-QUAL-002 warning), LICENSE
  (present), README accurate for an outside reader, CONTRIBUTING/AGENTS current.
- **Self-scan clean**: `keur scan` over the repo with the full catalogue at the
  repo's own maturity — no BLOCK findings keur itself would reject.

### Step 3 — Go public

Flip visibility only when steps 1-2 are green. Announce via the vision doc.

### Step 4 — Distribution, in order of effort/payoff

- **brew (first — trivial).** cpm already ships a Homebrew formula from its
  release-publish workflow; copy that pattern. The Release workflow already
  builds the macOS/Linux binaries; add a formula-publish step pointing at the
  GitHub Release assets.
- **npx (second — wrapper).** keur is a C++ binary, not a Node package, but the
  binary-over-npm pattern is proven (esbuild/swc): a thin npm package whose
  postinstall (or per-platform `optionalDependencies`) downloads the matching
  binary from the GitHub Release. `npx keur` then works without a toolchain.
- **apt (last — most upkeep).** A `.deb` + an apt repo (or a PPA) to host it.
  Heaviest to build and maintain; do it only once brew+npx cover most users, or
  skip in favour of a documented `curl | tar` install for Linux.

All three consume the same GitHub Release the one-click workflow already
produces — distribution is publishing the existing artifacts, not new builds.

### Step 5 — Absorb cpm's corpus (gate before decommission)

Per ADR-028: do NOT bulk-import cpm's ~1004 rules; adopt each through the eval
loop (measured precision) — roadmap M4. cpm cannot be decommissioned while it is
the only place a given check exists. So M4 (or at least parity on the checks
users rely on) gates step 6.

### Step 6 — cpm decommission (last)

Only after keur is public, installable via the same channels cpm uses, and
covers cpm's checks:

1. **Deprecate**: a notice in cpm's README + release notes pointing at keur.
2. **Freeze**: no new rules in cpm (already maintenance per ADR-028).
3. **Redirect channels**: keep cpm's brew/npx/apt installable for a grace
   period, but have them print a "migrate to keur" notice; do not hard-break
   existing users.
4. **Archive**: GitHub archive the cpm repo once telemetry/time shows migration.

Pulling cpm's apt/npx packages before keur occupies those channels would break
current cpm users — hence this is strictly after step 4.

## Why this order

- Public-before-MVP ships a tool that fails its own roadmap. Rejected.
- Public-before-history-audit risks leaking PII/BII irreversibly (a public repo's
  history cannot be un-seen). This is the one irreversible step, so it gets the
  hardest gate.
- Decommission-before-parity strands cpm users with no replacement for checks
  only cpm has. Rejected — M4/parity gates it.
- Channels-before-public is impossible (nothing to install yet).

## Consequences

- **Positive**: a safe, reversible-until-the-last-moment path; the irreversible
  step (public) is behind the strongest gate (history PII audit).
- **Positive**: distribution reuses the one-click Release artifacts — brew/npx/apt
  are publish steps, not new build pipelines.
- **Cost**: the history PII audit may require a history rewrite (squash/filter)
  before public — plan for it, do not discover it at flip time.
- **Dependency**: cpm decommission is downstream of M4 (ADR-028's eval-loop
  adoption), which is the largest remaining track.

## Follow-ups

1. Pre-public audit script: PII-vault scan over full history (ADR-042) +
   tracked-private-rules check + hygiene checklist. Run it, fix any hit.
2. Add SECURITY.md (clears KEUR-QUAL-002) before public.
3. brew formula publish step in the Release workflow (copy cpm's pattern).
4. npx wrapper package (binary download from the GitHub Release).
5. Decide apt vs documented tarball install for Linux.
6. cpm: deprecation notice + channel migration notices, gated on keur parity
   (ADR-028 M4).
