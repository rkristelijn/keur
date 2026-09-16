# ADR-002 — keur as a single compiled C++ binary

**Status:** Accepted  
**Date:** 2026-09-16  
**Replaces:** shell entrypoint `bin/keur` + `lib/*.sh` + `scanners/rules.sh`

---

## Context

keur started as a shell-based tool: `bin/keur` delegated to `lib/check.sh`,
`lib/scan.sh`, `lib/hook.sh`, and `lib/policy.sh`. The rule engine (`lib/rules.sh`)
was 213 lines of bash that bundled a YAML parser and a regex dispatcher.

ADR-001 proved that the hot path (rule matching) is fundamentally ill-suited for
shell: shell took ~5 min over the full cpm corpus; the C++ RE2 engine took 0.16s
(factor ~1875). The same conclusion applies to the outer shell:

- `check.sh` spawns sub-processes per scanner, parses JSON with grep, and has no
  type checking. Each invocation spends ~300ms on fork/exec overhead before a single
  line is scanned.
- `hook.sh` is 66 lines that do exactly one thing: copy files and chmod. That is
  simpler in C++ than in shell with all its quoting edge cases.
- `policy.sh` is 20 lines that read one TOML value. In C++ the TOML reader already
  lives in `rule_engine.cpp`.
- `scanners/rules.sh` is a wrapper that does nothing once `bin/keur-rules` exists.

Additionally: shell has no unit-test support, no type safety, and grows complex
around error handling (set -euo pipefail, traps, subshells). C++ has doctest (same
as cpm), RE2, and compiles on macOS and Linux without modification.

## Decision

keur will be a single compiled C++ binary: `bin/keur`.

Layout:

```text
src/
  rules/            existing rule engine (ADR-001, unchanged)
    rule_engine.h
    rule_engine.cpp
  keur/             new: subcommands + entry point
    main.cpp        parses argv, dispatches to cmd_*
    cmd_check.cpp   staged diff + rule engine in-process + policy
    cmd_check.h
    cmd_scan.cpp    directory scan via rule engine
    cmd_scan.h
    cmd_hook.cpp    hook install / global / status / remove
    cmd_hook.h
    cmd_policy.cpp  policy explain (read TOML, print thresholds)
    cmd_policy.h
    keur_test.cpp   doctest unit tests (doctest.h from vendor/)
  Makefile          builds bin/keur, links rule_engine.o
vendor/
  doctest.h         copied from cpm/vendor/ (same version, same licence)
```

The git hook stubs in `.git/hooks/pre-commit` / `pre-push` remain one-line shell
(`exec keur check --tier pre-commit`). This is unavoidable: git invokes hooks as
executable scripts. No logic lives in the stubs.

The `eval/` tooling (evaluate.sh, gen-cases.sh, loop.sh) stays outside the core
binary. Python/shell is defensible there: these are dev-time tools, not the deployed
hot path.

Existing shell scripts (`lib/*.sh`, `scanners/*.sh`, `bin/keur`) are deprecated but
not immediately deleted. They receive a `# DEPRECATED: use bin/keur` header.
`bin/keur-rules` remains as a standalone tool (useful for CI steps that only need the
rule engine).

## Consequences

**Positive:**

- No Python, no shell logic in the core.
- `keur check` exit latency: from ~300ms (fork+exec chain) to <10ms (in-process,
  pre-compiled RE2).
- Testable with doctest, same pattern as cpm (TEST_SUITE / SCENARIO / GIVEN /
  WHEN / THEN).
- One binary to distribute: `curl .../keur | install -m 755 /usr/local/bin/keur`.
- `keur hook --install` writes a one-line hook stub; no template copy needed.
- KEUR_HOME no longer required (binary resolves its own prefix from `argv[0]`).

**Negative / risk:**

- RE2 + abseil are build dependencies (already present from ADR-001).
- Windows build requires adaptation (same approach as cpm: `platform_win32.cpp`).
  Out of scope for now; keur targets macOS + Linux.
- `eval/` tooling still uses Python + shell; that remains accepted tech debt.

## Alternatives considered

- **Go single binary**: comparable UX, but introduces a second language and build
  chain. RE2 via cgo is more awkward than native C++.
- **Rust**: same argument. High learning cost for little gain on top of cpm's C++.
- **Keep shell**: proven too slow (ADR-001) and not unit-testable.
- **Python CLI (click/typer)**: no compile step, but slower startup, Python version
  dependencies, and cannot be distributed as a self-contained binary.
