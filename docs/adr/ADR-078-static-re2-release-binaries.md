# ADR-078 — Release binaries link RE2 and Abseil statically

@id ADR-078
**Status**: accepted · **Date**: 2026-11-22 · **Coverage**: code · **Relates to**: ADR-002 (single binary), ADR-005 (platform abstraction), ADR-045 (distribution channels), ADR-069 (pre-publish dry-run gates), issue #36

## Context

ADR-002 chose one compiled binary so keur has "one artifact to distribute and no
runtime language dependency". The v0.2.0 release did not deliver that. Each release
binary linked RE2 as a shared library from the platform package manager:

| Asset | Dynamic dependency |
|---|---|
| `keur-macos-arm64` | `/opt/homebrew/opt/re2/lib/libre2.11.dylib` (and through it Abseil) |
| `keur-linux-amd64` | `libre2.so.10` |
| `keur-windows-amd64.exe` | `libre2-11.dll` |

Consequences seen on 2026-11-22: on macOS, Homebrew upgraded Abseil while RE2 still
referenced the previous Abseil soname, and every keur binary on the machine (including
the one behind the git hooks) crashed at startup with `dyld: Library not loaded`.
A host without Homebrew RE2, or a Linux distribution without that soname, cannot
start keur at all. Every channel shipped the same binary: curl `install.sh`, `npx`,
the Homebrew formula and the `.deb`. CI did not notice because every job installed
RE2 before building and testing.

## Decision

1. **Build RE2 and Abseil from pinned source as static archives.**
   `scripts/build-static-deps.sh` downloads fixed versions (RE2 and an Abseil LTS,
   pinned in that script only), checks each tarball against a pinned SHA-256, and
   builds them with CMake (`BUILD_SHARED_LIBS=OFF`) into `.deps/static`. CI caches the
   prefix per OS, architecture and script hash, so a version bump rebuilds it.
2. **Link them into the binary.** `make build STATIC_RE2=<prefix>` links `libre2.a`
   and every `libabsl_*.a`. GNU ld gets a `--start-group` so archive order does not
   matter, plus `-static-libstdc++ -static-libgcc` (and `-static` on MinGW). macOS
   keeps the system `libc++` and `libSystem`, which every macOS has. Without
   `STATIC_RE2` the local developer build stays as before (shared, fast).
3. **Control: a binary that loads RE2/Abseil dynamically fails CI.**
   `scripts/check-binary-deps.sh` reads the dynamic library list (`otool -L`,
   `readelf -d`, `objdump -p`) and fails on any `re2`/`absl` entry. It runs in the CI
   build matrix and in the release workflow before upload.
4. **CI builds what users get.** The build matrix, `dist-smoke` and `deb-lint` use
   the static build and no longer install the package-manager RE2. A job that only
   passes because RE2 is installed on the runner can no longer hide this class.

## Consequences

- Release binaries run on a machine without RE2/Abseil and survive package-manager
  upgrades. Verified locally on macOS: the static binary needs only `CoreFoundation`,
  `libc++` and `libSystem`, and the full test suite passes against it.
- Binary size grows (RE2 and the used Abseil parts are now inside).
- RE2/Abseil updates are deliberate: change the pinned version and checksum in one
  script. Security fixes in those libraries therefore need a keur release, which is
  the normal model for a statically linked tool.
- The first CI run per platform builds the dependencies (about 35 s on an M-series
  Mac); later runs restore the cache.
- Temporary: the `dist-smoke` npx step installs `libre2-10` because it runs the
  already **published** v0.2.0 binary. Remove that step after the first static release.
- Not covered: the armhf QEMU job and the clang-tidy lint job still build against the
  system RE2. Neither produces a released artifact.
