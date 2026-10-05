# src/keur/ — the keur application

The CLI product. Builds `bin/keur`. This is the layer around the rule engine
([`src/rules/`](../rules/)): it wires subcommands, isolates I/O behind adapters,
reads config, renders output and TUIs, and links the engine in as its lower
layer.

Build + test from here:

```bash
make build          # bin/keur
make test           # unit + integration + e2e
make test-unit      # fast, pure-function suite
make help           # list all targets
```

## Folder map

| Dir | Responsibility |
|-----|----------------|
| `cmd/` | subcommand orchestrators — one file per command: `check`, `hook`, `scan`, `policy`, `train`, `learn`, `rules`, `config`, `manager`, `level`, `difficulty`, `settings`, `tune`. Orchestration only; logic lives in pure helpers. |
| `adapters/` | the **only** place external I/O happens — `git`, `fs`, `proc`, `sysinfo`. Injectable seams (ADR-003), enforced by KEUR-ARCH-001. |
| `config/` | config layer — `ruleset` (difficulty resolution), `scopes`, `hookmgr`, `scanners`, `disabled`, `keurignore`. |
| `scan/` | scanner dispatch + finding mappers + JSON helpers. |
| `report/` | the findings output provider (jsonl / compact / verbose, themeable colour). |
| `render/` | pure rendering — `box`, `display_width`, `logo` (+ baked `logo_generated.cpp`). No I/O. |
| `manager/` | the `keur manager` TUI — pure tree `model` + reducers, pure `view`, and the baked rule `catalog_generated.cpp`. |
| `learn/` | the `keur learn`/`train` monitor — `history` (JSONL parse), `sysinfo` (resources), `monitor` (render). |
| `common/` | platform abstraction (ADR-005) — `platform.h`/`compat.h` + the per-OS `platform_posix.cpp` / `platform_win32.cpp` (the only `#ifdef`s in the codebase) and the pure `keyinput` decoder. |
| `util/` | shared helpers (`keur_utils.h`, `tokenizer`). |

`main.cpp` is the entry point / subcommand router. `keur_test.cpp` is the
doctest-based test binary (unit / integration / perf suites).

## Layering rules (dogfooded)

- I/O only in `adapters/` — KEUR-ARCH-001, `tests/e2e/test_selfcheck.sh`.
- OS-specific code only in `common/` — KEUR-PORT-001..005,
  `tests/e2e/test_portability.sh`.
- Namespaces mirror the folder — KEUR-ARCH-002..004.

See [`../README.md`](../README.md) for the engine-vs-app split and the two-Makefile
rationale, and `docs/adr/ADR-003-provider-architecture.md` for the adapter design.
