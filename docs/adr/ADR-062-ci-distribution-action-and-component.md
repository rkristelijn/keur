# ADR-062 — CI distribution: a GitHub composite Action and a GitLab CI/CD standard component

@id ADR-062

## Status

Proposed · 2026-10-25 · Coverage: none
· Relates to: ADR-045 (go-public distribution sequence — brew/npx/apt; the same
public-release-assets prerequisite gates these CI channels and this ADR is a
sibling to those channels), ADR-012 (pipeline coverage — a keur gate *is* one of
the security stages this models; a shipped Action/component is how a repo adds
the `secret-scan`/`sast` stage), ADR-002 (single prebuilt binary — the thing both
channels fetch), ADR-051 (install the ruleset alongside the binary — a CI run
needs the rules too, not just `bin/keur`), ADR-041 (tiered CI — `pre-commit` vs
`pre-push` maps onto the CI tier the caller selects), the Release workflow
(`.github/workflows/release.yml`) and `install.sh` (the OS/arch + checksum
contract both channels reuse)

## Context

keur already ships as **one prebuilt binary per platform**, published to a
GitHub Release with a `SHA256SUMS`, installed by `install.sh` through a clean
env-contract (`KEUR_VERSION` pins the tag, `KEUR_BIN_DIR` picks the target dir;
the script detects OS/arch, fetches the matching `keur-<os>-<arch>` asset, and
verifies the checksum *before* trusting it — ADR-002, `install.sh`). The hook
story (`keur hook --install`) and the human install story (brew/npx/apt,
ADR-045) are covered. What is **not** covered is the other place keur belongs:
inside CI, where a pipeline wants to run `keur check`/`keur scan` as a stage
(the `secret-scan`/`sast` moment of ADR-012).

Today a CI user has to hand-write that: a step that curls `install.sh`, puts the
binary on `PATH`, maybe syncs the rules (ADR-051), and calls `keur`. Every repo
re-invents it, and a copy-pasted install snippet is exactly the stale-snapshot
class of drift ADR-051/ADR-061 warn about. The two dominant CI ecosystems each
have a first-class packaging format for "a reusable step" — GitHub **Actions**
and GitLab **CI/CD components** — and keur should ship one of each so adding the
gate is a few lines, not a bespoke script.

The crucial enabler: because keur is a single binary with a clean env-contract,
CI integration is **"fetch the binary, run `keur check`"** — there is no
toolchain to provision, no compiler, no language runtime (ADR-002). That makes a
*thin wrapper* sufficient; the wrapper does not build anything, it reuses the
same Release assets `install.sh` already consumes.

## Decision

Offer keur as two CI integration artifacts, both thin wrappers over the existing
binary + `install.sh` contract:

### 1. GitHub composite Action — `action.yml` at the repo root

A composite Action (`runs.using: composite`) whose steps:

1. run `install.sh` (honouring `KEUR_VERSION`/`KEUR_BIN_DIR`) to fetch the
   checksum-verified binary for the runner's OS/arch, and sync the bundled
   ruleset (ADR-051) so the run is not pointed at an empty rule dir;
2. invoke `keur check` (or `keur scan` for an advisory, non-blocking run),
   passing through the tier (`--tier pre-commit|pre-push`, ADR-041) and config
   inputs.

Consumers add it with the standard `uses:` reference:

```yaml
# .github/workflows/keur.yml (consumer side)
- uses: rkristelijn/keur@v1
  with:
    tier: pre-commit        # or pre-push
    # version: v0.1.0       # optional; defaults to latest (KEUR_VERSION)
```

Inputs map onto the env-contract (`version` → `KEUR_VERSION`, etc.) so the
Action adds no new surface — it is the `install.sh` + `keur check` contract
expressed as Action inputs.

### 2. GitLab CI/CD standard component — `templates/keur.yml`

A CI/CD component (the `templates/` layout GitLab's component spec requires,
publishable to the **CI/CD Catalog**) exposing a job template a pipeline
`include`s:

```yaml
# .gitlab-ci.yml (consumer side)
include:
  - component: gitlab.com/rkristelijn/keur/keur@v1
    inputs:
      tier: pre-commit
      # version: v0.1.0
```

The template's job runs the same two steps: `install.sh` to fetch the verified
binary + rules, then `keur check`. Inputs mirror the Action's, so the two
channels are behaviourally identical — same binary, same env-contract, same
verdict.

### Why composite (and a GitLab template), not a Docker action/image — for v1

- **No toolchain to ship.** A Docker action/image exists to carry a runtime or
  tools; keur needs neither (ADR-002). A composite Action / plain-job template
  that fetches one static binary is the minimal thing that works.
- **Reuses the one artifact.** Both wrappers call `install.sh` against the
  existing Release (ADR-045 step 4's "distribution is publishing the existing
  artifacts, not new builds" — the same logic applies to CI). No second build
  pipeline, no image registry to maintain and scan.
- **Transparent + checksum-verified.** The install path is the audited
  `install.sh` with its verify-before-trust step; a prebuilt image would move
  trust into an opaque layer the consumer cannot `less` the way they can the
  script.
- **Cross-OS by the same detection.** The composite Action runs on
  linux/macOS/windows runners and lets `install.sh`'s existing OS/arch detection
  pick the asset, rather than pinning a single Linux container image.

The trade-off accepted: a composite Action re-runs the download each pipeline
(mitigable with the runner's tool cache / `KEUR_BIN_DIR` on a cached path),
whereas a pre-pulled Docker image would be warm. For v1 the simplicity and
"one artifact, no registry" win outweighs a sub-second download; a Docker action
remains a possible **later** optimisation if cold-fetch latency ever matters,
not a v1 need.

### The hard prerequisite: both channels need public release assets

Both the Action and the component **download release assets at run time** — the
same `keur-<os>-<arch>` + `SHA256SUMS` that `install.sh` fetches. A CI runner
pulling from a **private** GitHub Release cannot do so without credentials, and a
public CI/CD Catalog entry pointing at a private asset is useless to anyone else.
So **both channels sit behind the exact same blocker as brew/npx in ADR-045**:
they require publicly downloadable release assets, which requires the repo to be
**public first**, which requires the pre-public history audit (ADR-045 step 2)
to pass. This ADR therefore slots *alongside* ADR-045's step 4 (distribution
channels) — it is another consumer of "the repo is public and the Release is
open", not an independent track. Nothing here is shippable before ADR-045's
gate is green.

## Consequences

- **Positive**: adding keur as a CI stage becomes a `uses:` / `include:` line
  instead of a hand-written install script — the same "repeat links, not facts"
  discipline (ADR-061) applied to CI config, killing the copy-pasted-snippet
  drift.
- **Positive**: both wrappers reuse `install.sh` and the single Release artifact
  (ADR-002/ADR-045), so there is no new build, no image registry, and the
  checksum-verify contract is preserved end to end.
- **Positive**: a shipped Action/component is the concrete way a repo satisfies
  the `secret-scan`/`sast` stage keur's own pipeline-coverage model expects
  (ADR-012) — keur becomes installable *as a pipeline stage*, not only as a local
  hook.
- **Positive**: the two channels are behaviourally identical (same binary, same
  env-contract, same verdict), so cross-platform/cross-forge behaviour does not
  diverge.
- **Cost / dependency**: neither channel ships until the repo is public with open
  release assets (ADR-045) — they are gated on the same irreversible step and
  must not be announced before it.
- **Cost**: a composite Action re-downloads per run (sub-second, cacheable); if
  that ever becomes a bottleneck, a Docker action is the documented next step,
  not a v1 obligation.
- **Risk (named, bounded)**: the wrappers must keep their inputs in lockstep with
  the `install.sh` env-contract (`KEUR_VERSION`, `KEUR_BIN_DIR`) and the rule-sync
  behaviour (ADR-051); a drift there would make a CI run scan a stale/empty
  ruleset — the same silent-false-negative ADR-051 fixes. Mitigate by having both
  wrappers *call* `install.sh` rather than re-implementing its logic.

## Follow-ups

1. Author `action.yml` (composite) at the repo root: inputs `version`, `tier`,
   `config`, `mode` (check|scan); steps = run `install.sh` → sync rules (ADR-051)
   → run `keur`. Document it in the README "More info" / a `docs/` how-to, linked
   not copied (ADR-061).
2. Author `templates/keur.yml` (GitLab CI/CD component) with mirrored inputs;
   prepare the CI/CD Catalog publication metadata.
3. Gate both on ADR-045 step 3 (repo public) + step 4 (channels) — do not
   publish to the GitHub Marketplace or the GitLab CI/CD Catalog before the
   release assets are public.
4. Wire an example consumer pipeline (one GitHub workflow, one `.gitlab-ci.yml`
   snippet) that satisfies the ADR-012 `secret-scan`/`sast` stage via the
   Action/component, as the documented "add keur to your pipeline" path.
5. Decide input-pinning policy: default `version: latest` vs. recommend pinning a
   tag in CI (CI should pin for reproducibility — the opposite of the local
   install default).
