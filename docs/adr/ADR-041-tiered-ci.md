# ADR-041 — Tiered CI: gate by PR title-state depth × affected-paths breadth

@id ADR-041

## Status

Proposed · 2026-10-05 · Coverage: none
· Relates to: ADR-005 (platform abstraction — the 3-OS build matrix this tiers),
ADR-006 (word-size portability — the arm32 QEMU build this defers),
ADR-018 (cpm second gate), ADR-008 (incremental compile / build-affected —
the local analogue of the breadth axis),
`.github/workflows/ci.yml` (the workflow this restructures),
ADR-040 (engine coverage gaps — unrelated, but same PR-driven Sonar context)

## Context

Every push to a PR branch currently runs the **full** workflow: `build` on three
OSes (Linux, macOS, Windows), `build-arm32` under QEMU, and `dogfood`. Observed
wall-clock on the PR #9 merge run:

| Job | Time |
|-----|------|
| build macos-arm64 | 1m40s |
| build linux-amd64 | 2m34s |
| build windows-amd64 | 4m5s |
| dogfood | 2m14s |
| SonarCloud | 5m31s |
| **build-arm32 (QEMU)** | **19m3s** |

The arm32 QEMU build dominates — ~19 min, and it reran on a commit that was
**pure markdown/shell reformatting** (`style(docs,scripts): rumdl + shfmt`). That
is the waste this ADR removes: the slowest, least-relevant job ran on a change
that could not possibly affect a 32-bit binary.

Two independent facts make the full run wrong for most pushes:

1. **Not every change needs every job.** A docs-only or rules-only (`.rule` data,
   ADR-009) change needs no compile at all; a change under `src/keur/**` needs
   the build but a docs change does not. This is the **breadth** axis — *what
   did the change touch?*

2. **Not every push is a merge candidate.** A draft PR is work-in-progress; a
   ready-for-review PR invites eyes; a merge is the only moment the full
   cross-platform + 32-bit proof actually gates anything. This is the **depth**
   axis — *how far along is this change?*

There is **no branch protection** on the repo (recorded in the 2026-10-03
handover): CI never *blocks* a merge, it is advisory. That makes it safe to tier
aggressively — we are optimising signal/cost, not weakening a gate that was
enforcing anything.

## Decision

Gate CI jobs on two axes: **title-state depth** (how ready) × **affected-paths
breadth** (what changed). A job runs only when both its depth threshold and its
path filter are satisfied.

### Axis 1 — depth (PR title-state)

GitHub exposes the draft flag and the `ready_for_review` event (already in the
workflow's `on.pull_request.types`). Map the lifecycle to three depth bands:

| State | Trigger | Jobs that run |
|-------|---------|---------------|
| **Draft** | PR is draft (`github.event.pull_request.draft == true`) | fast only: `build` (Linux) + `dogfood` |
| **Review** | PR is non-draft (ready for review) | the above + macOS + Windows + SonarCloud |
| **Merge** | push to `main`, or `ready_for_review` into a merge | **everything**, incl. `build-arm32` |

The arm32 QEMU build — the 19-min job — runs **only at the merge tier**, not on
an in-progress draft push. Rationale: 32-bit word-size correctness (ADR-006) is
a property of the *merged* code; proving it on every draft iteration buys nothing
and costs the most.

### Axis 2 — breadth (affected paths)

Use a paths-filter step (`dorny/paths-filter`, pinned to a commit SHA per the
repo's Sonar-driven SHA-pin policy) to classify the diff into buckets, then make
each job depend on the relevant bucket:

| Bucket | Globs | Gates which jobs |
|--------|-------|------------------|
| `code` | `src/**`, `**/Makefile`, `.github/workflows/ci.yml` | all `build*`, `dogfood`, Sonar |
| `rules` | `rules/**`, `config/**` | `dogfood` (rules are data — no compile; ADR-009) |
| `docs` | `docs/**`, `**/*.md` | none (lint runs in the pre-commit hook locally) |

A docs-only change therefore triggers **zero** compile jobs. A rules-only change
runs `dogfood` (which does need a built binary to apply the policy) but skips the
cross-platform matrix. Only a `code` change pays for the full build.

This is the CI analogue of ADR-008's local `build-affected`: the breadth axis is
"affected paths", Nx-style.

### Interaction (the matrix)

```text
            breadth →   docs-only     rules-only        code
depth ↓
 Draft                  (nothing)     dogfood           build(linux) + dogfood
 Review                 (nothing)     dogfood           + macOS + windows + sonar
 Merge                  (nothing)     dogfood           + build-arm32 (QEMU)
```

A job runs iff its depth band is reached **and** its path bucket changed. The
cheapest useful signal (Linux build + dogfood) is always available early; the
expensive proof (arm32) is paid once, at the end, only when code changed.

### arm32 cost reduction (independent of tiering)

Even confined to the merge tier, 19 min is slow. Reduce it so the merge gate is
not painful:

1. **Pre-built toolchain image.** The current job spends minutes in
   `apt-get install g++ make libre2-dev libabsl-dev …` inside the emulated
   container on every run. Bake a `arm32v7/debian:bookworm` + toolchain image
   once, push to GHCR, and `docker run` that — the install cost moves to a
   rare image rebuild.
2. **`make -j`** on the arm build (currently a bare `make -C src/keur build`;
   the local fast loop already uses `-j8`).
3. **ccache** mounted into the container to reuse objects across runs.

These are follow-ups, not blockers for the tiering itself.

### Sonar: automatic → CI-based analysis

SonarCloud is currently *automatic analysis* (triggered by GitHub, outside this
workflow), so the workflow cannot gate it on `draft == false`. Move it to
**CI-based analysis** (a job in `ci.yml`) so the depth axis can skip it on drafts.
This also lets the `code` path-filter skip Sonar on docs-only changes. (Sonar
remains advisory either way — see "no branch protection" above.)

## Consequences

- **Positive**: the common case (draft push, or a docs/rules change) goes from
  ~19 min to ~2–3 min. The 19-min arm32 job runs only at merge, and only when
  code changed — the exact waste from PR #9 (reformatting rerunning QEMU) is gone.
- **Positive**: faster draft iterations encourage pushing early as draft, which
  the depth model rewards with a cheap, fast gate.
- **Negative / risk**: a job skipped by a path filter reports *neutral/skipped*,
  not *success*. With no branch protection this is harmless, but if branch
  protection is ever added, required checks must use the "skipped == success"
  pattern (a tiny always-green job that `needs:` the filtered ones) or the merge
  button will block forever. Record this so it is not re-discovered painfully.
- **Negative**: `ready_for_review` only fires on the draft→ready transition, not
  on subsequent pushes while ready. The review-tier jobs must therefore key off
  `draft == false` on `synchronize`, not only the `ready_for_review` event.
- **Cost**: one paths-filter action (SHA-pinned) + per-job `if:` conditions +
  moving Sonar into the workflow. No new tooling to maintain beyond that.

## Follow-ups

1. Implement the depth `if:` conditions (`draft`, `ready_for_review`,
   `push → main`) across the `build`, `build-arm32`, `dogfood` jobs.
2. Add `dorny/paths-filter` (SHA-pinned) and wire the `code`/`rules`/`docs`
   buckets to the job conditions.
3. Pre-build + publish the arm32 toolchain image to GHCR; switch `build-arm32`
   to `docker run` that image with `make -j` + ccache.
4. Migrate SonarCloud from automatic to CI-based analysis so depth/breadth can
   gate it.
5. If branch protection is ever enabled, add the "skipped == success" aggregator
   job so filtered checks do not deadlock the merge button.
