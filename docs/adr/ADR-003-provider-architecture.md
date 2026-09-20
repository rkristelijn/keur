# ADR-003 — Provider / adapter architecture (codeflex)

@id ADR-003
**Status:** Accepted
**Date:** 2026-09-16
**Coverage**: code
**Builds on:** ADR-002 (single C++ binary)

---

## Context

After ADR-002 the subcommands worked but each reached for I/O directly:

- Two independent `popen("git ...")` sites (`cmd_hook.cpp` had `run_git`,
  `cmd_check.cpp` had its own `staged_files`).
- Findings JSONL was hand-assembled in two places (`cmd_scan.cpp` and
  `src/rules/main.cpp`), with a duplicated `jesc()` helper.
- Output went straight to `std::cout` / `std::cerr` everywhere, with no notion
  of an output mode (json / compact / verbose / colour).

This is the "same shape repeated" problem: query the environment, do work,
emit output. Left unchecked it becomes 40 slightly-different copies. The user's
requirement: one touch point per concern.

## Decision

Introduce thin **providers / adapters** — one integration point per external
concern. Each is a small struct of `std::function` members with a real default
implementation, so production code uses the default and tests inject a fake.

Layers:

| Layer | File | Single touch point for |
|-------|------|------------------------|
| git adapter    | `git.h/.cpp`      | all git queries (run, staged_files, hooks dir) |
| config provider| `cmd_policy.h/.cpp`| keur.toml resolution + scalar reads |
| output provider| `report.h/.cpp`   | findings → JSONL / compact / verbose / colour |
| rule dirs      | `keur_utils.h`    | KEUR_HOME / KEUR_RULES_DIRS resolution |

Principles:

1. **One implementation per concern.** No second `popen("git ...")`, no second
   JSONL assembler, no second TOML reader.
2. **Injectable seam.** Providers are structs of `std::function`, defaulted to
   the real implementation. Tests assign fakes — no real git / filesystem.
3. **Pure core, I/O at the edge.** Decision logic (policy_evaluate) stays pure;
   providers live at the boundary.
4. **Themeable output.** The output provider owns format + colour, so every
   subcommand's output is consistent and a single flag switches the theme.

## Enforcement (keur guards itself)

The layering is not just a convention — it is a control. keur enforces its own
architecture by dogfooding:

- **Rule `KEUR-ARCH-001`** (`rules/keur-internal/`) is a `block` rule that fires
  on raw `popen` / `system` / `mkdir` / `unlink` anywhere except `adapters/` and
  tests. External I/O outside the adapter layer fails the gate.
- **`tests/e2e/test_selfcheck.sh`** runs that rule over `src/keur` on every test
  run and asserts zero findings, then injects a temporary violation to prove the
  control actually fires (guards against a dead rule).

Isolating the adapters in `src/keur/adapters/` is what makes the rule a clean
one-liner (`exclude_paths: adapters/ ...`) instead of a fragile per-file list.

## Consequences

**Positive:**

- Duplication removed: one git spawn, one findings emitter, one TOML reader.
- Testability: subcommands take providers as parameters; fakes need no git repo.
- A new output mode (e.g. SARIF) is one change in the output provider, not N.
- Adding an external scanner (libmode: semgrep/gitleaks) plugs into the same
  findings model and emitter.

**Negative / cost:**

- A little indirection (`std::function` members) over direct calls. Acceptable:
  the call sites are cold (once per invocation), not the hot scan loop.

## Alternatives considered

- **Abstract base classes + virtual dispatch:** heavier; `std::function` structs
  are lighter and just as injectable for this size of tool.
- **Leave it as-is:** rejected — duplication was already appearing (git, JSONL).
