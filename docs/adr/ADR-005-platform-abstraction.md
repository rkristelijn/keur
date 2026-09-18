# ADR-005 — Platform abstraction layer (cross-platform build)

**Status:** Accepted
**Date:** 2026-09-18
**Builds on:** ADR-002 (single C++ binary), ADR-003 (provider / adapter architecture)
**Prior art:** cpm ADR-170 (multi-OS platform abstraction)

---

## Context

keur must build and run on macOS, Linux and — desirably — Windows. The code had
drifted the other way: OS-divergent primitives were scattered through the
codebase and, in one case, were flat-out macOS-only.

- `learn/sysinfo` read host resources with mach + `sysctlbyname` and
  `#include <sys/sysctl.h>` — this **broke the Linux build outright**.
- The Git / Fs / Proc adapters used POSIX-only constructs directly: `popen`,
  `std::system`, `command -v`, `WEXITSTATUS`, `2>/dev/null`, `mkdir`, `unlink`,
  and hardcoded `"/"` path separators.
- `cmd/learn` and `learn/monitor` reached for `<unistd.h>` (`usleep`),
  `<sys/ioctl.h>` (`TIOCGWINSZ`), and `kill()` for a liveness probe.

Centralising I/O in `adapters/` (ADR-003) was necessary but not sufficient: an
adapter with `#if defined(__APPLE__)` still spreads platform logic and still
fails to compile on the OS it forgot. The problem is *platform divergence*, not
just *I/O location*.

## Decision

Introduce a **platform abstraction layer** under `src/keur/common/`, selected at
compile time by translation unit — the cpm ADR-170 model:

| File | Role |
|------|------|
| `common/platform.h`       | the interface: free functions in `namespace keur::platform` |
| `common/platform_posix.cpp` | the macOS + Linux implementation |
| `common/platform_win32.cpp` | the Windows implementation |
| `common/compat.h`         | name-level shims (`popen`→`_popen`, `KEUR_PATH_SEP`, null device) |

The Makefile picks the right translation unit:

```make
ifeq ($(OS),Windows_NT)
  PLATFORM_SRC = common/platform_win32.cpp
else
  PLATFORM_SRC = common/platform_posix.cpp
endif
```

The interface covers every primitive that diverges across OSes: `os_kind`,
`path_sep`, `path_join`, `wait_exit`, `run`, `run_argv`, `capture`,
`capture_bytes`, `tool_on_path`, `make_dir`, `path_exists`, `write_executable`,
`remove_file`, `read_resources`, `sleep_ms`, `terminal_size`, `process_alive`.

Principles:

1. **No `#ifdef` in business code.** The *only* place a platform guard may live
   is `common/`. `platform_posix.cpp` carries a single `__APPLE__` split (mach
   vs `/proc`); `platform_win32.cpp` needs no guard at all — it is only ever
   compiled on Windows.
2. **The adapters delegate.** Git / Fs / Proc / SysInfo keep their ADR-003
   injectable-seam shape, but their `default_*` implementations now call
   `keur::platform::…` instead of raw syscalls. The seam and the platform layer
   compose: tests still inject fakes; the real path is portable.
3. **Path building goes through `platform::path_join`.** No hand-concatenated
   `"/"`.
4. **Genuine POSIX-shell artifacts are the one exception.** The git-hook stub
   strings in `cmd/hook.cpp` are `#!/bin/sh` scripts — git runs them via `sh`
   even on Windows (Git for Windows ships one), so they legitimately stay shell.
   They are string literals, not `system()`/`popen()` calls, so the portability
   rules do not (and should not) flag them.
5. **Constructed commands spawn shell-free.** When a command carries any
   non-literal input, business code calls `platform::run_argv` (via
   `Proc::run_argv`) with an argv vector and explicit `KEY=value` environment
   overrides — `posix_spawnp` on POSIX, `CreateProcessA` on Windows, no
   `/bin/sh` or `cmd.exe`. Nothing in `argv` can be reinterpreted as a shell
   metacharacter (CWE-78). The string form `run`/`capture` is reserved for fixed
   literal commands. This is enforced by `KEUR-SEC-006` (see below).

## Enforcement (keur guards itself)

Like ADR-003's architecture controls, the platform model is a control, not just
a convention. Five `block`/`warn` rules under `rules/keur-internal/`
(`KEUR-PORT-001..005`), adapted from cpm's STYLE-012/013/015/016/017 and
QUAL-078, fire on drift **outside `common/`**:

- **001** platform `#ifdef` (`_WIN32` / `__APPLE__` / `__linux__`)
- **002** OS-only header (`<windows.h>`, `<unistd.h>`, `<sys/sysctl.h>`, `<mach/…>`, …)
- **003** POSIX-only / raw-platform call (`WEXITSTATUS`, `lstat`, `fork`,
  `clock_gettime`, `QueryPerformanceCounter`, …)
- **004** hardcoded path separator (`+ "/"`)
- **005** POSIX shell command in `system()`/`popen()` (`mkdir -p`, `command -v`, …)

Two command-injection controls under `rules/security/` complete the picture:
`KEUR-SEC-005` blocks a non-literal command into the libc sinks
(`system()`/`popen()`); `KEUR-SEC-006` blocks a non-literal command into keur's
own shell sink (`Proc::run/.capture` → `platform::run/capture`), steering
callers to the shell-free `run_argv` path above. The sink layer itself
(`common/`, `adapters/`) is excluded — it is the one documented home for a
shell string.

`tests/e2e/test_portability.sh` runs these over `src/keur`, asserts zero
findings, then injects a violation to prove the controls fire (no dead rule).

## Cross-platform CI

`.github/workflows/ci.yml` builds on **ubuntu-24.04, macos-14 and
windows-latest**. RE2 + abseil come from apt (Linux), brew (macOS) and MSYS2
UCRT64 (Windows). Because RE2 is available on MSYS2, Windows gets a **full**
build with the rule engine — no `NO_RE2` fallback. After each build a
post-compile e2e smoke test (`tests/e2e/smoke.sh`) exercises the essential
subcommands (`version`, `help`, `policy explain`, `scan`, `check`). A Linux
dogfood job runs the architecture + portability self-checks as a drift gate.

## Consequences

**Positive:**

- The Linux build works (it did not before). macOS keeps its native readings.
  Windows builds the full engine.
- One place to reason about OS differences; business code reads the same on
  every platform.
- Drift is blocked at the gate, not discovered at build time on another OS.

**Negative / cost:**

- One extra object (`keur_platform.o`) and a small indirection over direct
  syscalls. Cold-path only (never the hot scan loop), so negligible.
- Windows remains the least-exercised target; CI compiles and smoke-tests it,
  but deep behaviour there is less battle-tested than POSIX.

## Alternatives considered

- **`#ifdef` inside one adapter file (the first attempt):** rejected — it still
  spreads platform logic, still trips STYLE-013/015, and mixes three OSes in one
  translation unit.
- **`std::filesystem` / `std::chrono` everywhere instead of a layer:** used
  where it already fits (run-duration timing), but it does not cover load
  average, RAM, terminal size, process liveness or `command -v`, so a thin
  platform layer is still needed.
