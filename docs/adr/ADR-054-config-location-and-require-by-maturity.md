# ADR-054 — Config location: `.config/keur.toml`, and require a repo config by maturity

@id ADR-054
**Status**: accepted · **Date**: 2026-10-08 · **Coverage**: code (`scopes.cpp` repo-dotconfig layer, `policy.cpp` upward search) + rules (KEUR-CFG-001/002) + tests · **Relates to**: ADR-011 (project profile), ADR-026 (maturity vs action), ADR-043 (path-scoped config), ADR-051 (distribution — shared vs per-machine config), ADR-052 (file-absence is a repo question), ADR-053 (clustering the warn/block overlap)

## Context

Two problems surfaced while raising keur's own gate to maturity 5.

**1. The repo config was gitignored and cluttered the root.** keur's own policy
lived in `<repo>/keur.toml`, which `.gitignore` excluded (it began life as a
per-machine local file). So the level-5 policy did not travel with the repo, was
not reviewable, and the self-check that enforces it ran against a file that
could differ per machine. Separately, projects dislike a root filling with
per-tool dotfiles (eslint, prettier, shellcheck, …) — a well-known complaint
(nodejs/tooling#71). keur only resolved `<repo>/keur.toml` (root) and the global
XDG path; there was no uncluttered in-repo location.

**2. "No repo config" was invisible.** A repo with no keur config silently ran
on built-in defaults. There was no signal that the gate's policy was implicit,
and no way to require an explicit, shared policy as a project matures.

## Decision

### A. Add `<repo>/.config/keur.toml` as the preferred repo config location

@id ADR-054.dotconfig

The config resolver gains a repo-scoped `.config/keur.toml`, tried **before** the
root `<repo>/keur.toml` (both still work; root stays as a per-machine override).
This is the XDG-in-repo convention: a project keeps its tool config under
`<repo>/.config/` so the root is clean. Implemented in both resolution paths —
`config/scopes.cpp` (the layer stack for difficulty) and `cmd/policy.cpp`
(`find_config_upward` for thresholds) — so difficulty and policy resolve the
same way.

`.gitignore` is narrowed from `keur.toml` (which matched everywhere, including
`.config/`) to `/keur.toml` (root only). The shared, reviewable policy now lives
in the **tracked** `.config/keur.toml`; the root `keur.toml` remains an ignored
local override. keur's own policy moved there and is now versioned.

### B. Require a repo config, escalating warn → block by maturity

@id ADR-054.require

Two file-absence rules (repo scope, ADR-052) check that a keur config exists at
repo level (basename `keur.toml`, so `.config/keur.toml` OR root counts):

- **KEUR-CFG-001** — maturity 2, `action: warn`. From "developing" up, a repo
  with no keur config gets a nudge.
- **KEUR-CFG-002** — maturity 4, `action: block`. From "managed" up, the same
  gap blocks the commit.

This uses ADR-026's orthogonal axes exactly as intended: `maturity` decides IF a
rule is active, `action` decides how hard it fails. A low-maturity repo sees only
the warn; a managed/hardened one must commit a policy. It is also
self-consistent: a repo with no config resolves to the default (low) level, so
only the warn fires — and the warn invites the config that would let it raise its
level, at which point the block guards against later removal.

## Consequences

**Positive**
- keur's level-5 policy is tracked, shared and reviewable; the self-check
  (`test_maturity.sh`) runs against a committed file, not a per-machine one.
- Projects get an uncluttered config home (`.config/`) without losing the root
  `keur.toml` option.
- "No explicit gate policy" is now visible (warn) and, at maturity, enforced
  (block) — strictness scales with the profile, not by hand.

**Negative / risk**
- Two near-identical rules (CFG-001/002) are the "same issue, two rules" overlap
  ADR-053 will cluster in the report. Accepted for now: it is the sanctioned
  ADR-026 pattern for warn-low / block-high.
- An extra resolved config layer per run. Negligible (a path probe), and it made
  three existing scope tests assert the new ordering — updated, all green.

## Testing

- Scope tests updated for the extra `.config/` repo layer (ordering + existence).
- KEUR-CFG-001/002 verified: no finding on keur itself (it has `.config/keur.toml`);
  on a config-less repo the warn fires at default level and the block only once
  maturity ≥ 4 is resolved.
