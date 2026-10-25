# ADR-065 — Keep build-confidence high without a slow gate: fast sync, heavy async

@id ADR-065

**Status**: Proposed · **Date**: 2026-10-25 · **Coverage**: none
· Relates to: ADR-041 (tiered CI by cost — this is its local-side twin), ADR-050
(CI pipeline ordering cheap→expensive — the same ordering, applied before push),
ADR-035 (self-explaining / no `--no-verify` — a slow gate is what drives the
bypass this guards against), ADR-005/ADR-006 (the cross-platform build this
raises confidence in), ADR-002 (single binary; the hook must stay sub-second),
`scripts/ci-local.sh` (the existing local stage-runner this extends), the `keur
train` background supervisor (`eval/supervise.sh` — the async pattern reused)

## Context

CI was tiered by cost (ADR-041): a PR runs only the cheap Linux build; the full
macOS/Windows/arm32 matrix runs on `main`. That saves money but opens a gap —
**how do we keep the chance of a green cross-platform build high when that build
no longer runs on every PR?**

The obvious answer — run the full matrix locally before every push — is wrong:
a Docker/QEMU multi-platform build takes minutes. A multi-minute **synchronous**
pre-push gate is exactly what makes a developer reach for `git push --no-verify`
(the anti-pattern ADR-035 exists to prevent). A gate that is too slow is a gate
that gets bypassed, which is no gate at all. The constraint is firm: **nothing
heavy may run synchronously on commit or push.**

So confidence must come from two places that are *not* a blocking gate:
1. checks that are already **sub-second**, which can stay synchronous because
   the developer never feels them; and
2. the heavy cross-platform builds, moved **off the critical path** — run
   asynchronously so they inform rather than block.

keur already has the pieces: the `KEUR-PORT-*`/`KEUR-ARCH-*`/`KEUR-CPP-*`
self-check rules run in milliseconds (they grep, they do not build), and
`keur train` already runs work as a supervised background job with a live
monitor (`eval/supervise.sh`). The async machinery exists; this ADR applies it
to build-confidence.

## Decision

Three layers, split by **speed**, not by all-or-nothing:

### Layer 1 — synchronous, sub-second (stays on commit/push)

The pre-commit/pre-push hook keeps running only what is already instant:
- the `KEUR-PORT-*` / `KEUR-ARCH-*` / `KEUR-CPP-*` self-check rules — these
  catch the *predictable* cross-platform breakers statically (a POSIX call
  outside `common/`, a platform `#ifdef` in the wrong layer, `emplace_back` on
  an aggregate = the macOS libc++ `construct_at` trap seen repeatedly in the
  changelog). This is the single highest-leverage, zero-cost confidence source.
- an incremental compile of the staged translation units (fast; most pushes
  touch few files).

The developer does not feel this. It never shells out to Docker/QEMU.

### Layer 2 — asynchronous, local, non-blocking (after push, informs)

The heavy cross-platform builds (a Docker `ubuntu:24.04` build = the Linux
runner 1:1; a QEMU `arm32v7` build = the arm32 job 1:1) run as a **background
job**, reusing the `keur train` supervisor pattern:
- triggered right after a push (or on demand), detached — it does not hold up
  the push;
- the developer keeps working; the result arrives out-of-band (a desktop
  notification / a status line / a `keur build --status` dashboard, like
  `keur train --status`);
- a failure is surfaced loudly but after the fact, with the self-explaining
  message (ADR-035) naming which platform + what broke.

This gives native-Linux + native-macOS + emulated-arm32 confidence — **3 of the
4 CI platforms, local, free, and off the critical path.** Windows/MSYS2 is the
only platform too costly to emulate locally; it is left to CI-on-main.

### Layer 3 — CI on main (confirmation, not discovery)

The full matrix on `main` (ADR-041) becomes a *confirmation* of what layer 2
already proved, plus the one genuinely CI-only check (Windows). By the time code
reaches `main`, a break is unlikely — CI stops being a debug-by-push loop.

### The rule, stated once

**Synchronous = only sub-second. Anything that builds runs async or in CI.**
A gate the developer waits minutes for is a gate they disable; a gate they never
feel is a gate that survives.

## Consequences

- **Positive**: high cross-platform confidence (3/4 platforms proven locally)
  with a hook that stays sub-second — no push latency, no `--no-verify`
  pressure. Directly answers the "keep the build green without running the
  build on every push" question.
- **Positive**: reuses existing machinery — the self-check rules and the
  `keur train` supervisor — rather than inventing a new subsystem (ADR-002
  "one artifact, proven tricks").
- **Positive**: CI spend stays low (ADR-041) *and* main rarely breaks, because
  the async layer catches breaks before they are pushed to main.
- **Neutral**: async results are eventual, not immediate — a developer can push
  and then learn minutes later that arm32 broke. Acceptable: it is a feature
  branch, not main, and the break is caught before the merge/release.
- **Cost / open questions**: the background build runner (`keur build --watch`
  or a git `post-push`-style hook + supervisor) must be built; result delivery
  (notification vs. status dashboard) to be designed; Docker/QEMU must be
  present for layer 2 (degrade gracefully to "layer 1 + CI" when absent, never
  block).

## Follow-ups

1. Extend `scripts/ci-local.sh` with a `--full` that runs the Docker-Linux and
   QEMU-arm32 builds — the synchronous, opt-in form (for "I want to be sure now").
2. Wrap that in an async supervisor (reuse `eval/supervise.sh`) triggered after
   push, with a `keur build --status` monitor mirroring `keur train --status`.
3. Keep layer 1 (the sub-second self-check rules + incremental compile) as the
   only thing the hook runs synchronously.
