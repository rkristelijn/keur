# ADR-006 — Word-size portability (32-bit / 64-bit)

@id ADR-006
**Status:** Accepted
**Date:** 2026-09-18
**Coverage**: none <!-- proven by a CI build target, not a code site -->
**Builds on:** ADR-005 (platform abstraction layer)

---

## Context

keur should run on 32-bit targets — concretely an older 32-bit-only Raspberry
Pi (Pi 1 / 2 / Zero / Zero W, ARMv6/ARMv7, `armhf`) — as well as the usual
64-bit desktops and servers. The question that prompted this ADR: should keur
be able to *select* a 32-bit vs 64-bit "solution" at build or run time, e.g.
via a helper or adapter?

The answer is no, and it is worth writing down why, because the platform
abstraction of ADR-005 makes an adapter feel like the natural tool here — and
it is the wrong one.

## Decision

**Word-size is a compile-time target, not a runtime choice.** A binary *is*
32-bit or 64-bit; the compiler decides when it builds (`-m32`/`-m64`, or the
toolchain triple such as `arm-linux-gnueabihf` vs `aarch64-linux-gnu`). There
is nothing to select while the program runs, so there is no adapter and no
helper. An `if (is_64bit()) { … } else { … }` would be dead code — one branch is
unreachable in any given binary — and that is exactly the `#ifdef`-style
divergence the KEUR-PORT rules (ADR-005) exist to prevent.

Instead of *choosing*, keur *types wide enough to be correct on both*:

1. **`size_t` for sizes, indices and counts.** It is 32-bit on a 32-bit target
   and 64-bit on a 64-bit target — automatically correct, no branching.
2. **`int64_t` / `uint64_t` when a value genuinely needs 64 bits** regardless of
   word size (byte counts of large files, RAM in bytes, timestamps).
3. **Never store a pointer or a `size_t` in an `int`** (`(int)v.size()` truncates
   on a 64-bit host and is a portability smell even when the count is small).
4. **Do not assume `long` is 64-bit** — it is 32-bit on 32-bit Linux/ARM (LP32)
   and on all Windows (LLP64). Use `int64_t` when 64 bits are required.

This is enforced, not just documented: a `warn`-level rule
`KEUR-PORT-006-word-size` (rules/keur-internal/) flags `(int)`/`static_cast<int>`
truncation of `.size()`/`.length()` and 64-bit-assuming `long` storage, so the
codebase stays word-size-agnostic by construction.

## Proving it (CI, not a local build)

Correct typing is necessary but not sufficient — we prove the 32-bit build
actually works in the pipeline, so nobody has to own 32-bit hardware or a local
cross-toolchain:

- `.github/workflows/ci.yml` adds a **32-bit `armhf` build job** that runs under
  QEMU emulation in an `arm32v7/debian:bookworm` container. `armhf` matches the
  real target (32-bit Raspberry Pi OS is Debian-based), and Debian ships
  `libre2-dev` + abseil for `armhf`, so it is a full build with the rule engine.
- After the build the job runs the same **post-build smoke test**
  (`tests/e2e/smoke.sh`: version / help / policy explain / scan / check) that the
  64-bit jobs run. If the 32-bit binary mis-types a size or a time value, the
  smoke test is where it surfaces.

x86 `-m32` (i386) was rejected: Ubuntu does not reliably publish
`libre2-dev:i386` / `libabsl-dev:i386`, and i386 is not the target anyway. armhf
under QEMU is slower but representative of the actual hardware.

## Consequences

**Positive:**

- No dead 32/64 branching, no adapter for a non-runtime concern.
- The 32-bit target is continuously proven to build *and run* (smoke), not just
  assumed to work.
- The word-size rule keeps future contributions 32-bit-safe.

**Negative / cost:**

- The QEMU armhf job is slow (emulated) — acceptable for a CI job that only
  needs to compile + smoke, not run the full test matrix.
- Windows remains LLP64 (`long` is 32-bit there too); the same "don't assume
  `long` is 64-bit" rule covers it, so no separate handling is needed.

## Alternatives considered

- **A runtime 32/64 adapter/helper:** rejected — word-size is fixed at compile
  time, so the adapter would contain unreachable code and violate the ADR-005
  no-divergence-in-business-code principle.
- **i386 `-m32` build in CI:** rejected — 32-bit RE2/abseil dev packages are not
  reliably available on the Ubuntu runners, and i386 is not the real target.
- **No 32-bit coverage at all:** rejected — the user has a concrete 32-bit Pi use
  case, and "it probably compiles" is not proof. CI makes it proof.
