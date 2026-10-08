# Contributing to keur

keur is a single compiled C++ binary. The core is written in C++20, uses RE2 for
regex, and doctest for unit tests. All development happens under `src/keur` (the
subcommands and adapters) and `src/rules` (the shared rule engine).

## Prerequisites

- A C++20 compiler (clang or gcc).
- RE2 and abseil.

```bash
# macOS
brew install re2 abseil

# Debian / Ubuntu
sudo apt install libre2-dev libabsl-dev
```

The Makefile resolves RE2 and abseil via `brew --prefix` on macOS and falls back
to system paths on Linux. Override with `RE2_PREFIX` / `ABSL_PREFIX` if needed.

## Build

```bash
cd src/keur
make            # default target: build -> ../../bin/keur
make build      # explicit
make help       # list all targets
```

The version string is read from `config/keur.toml` at build time and passed to
the compiler as `-DKEUR_VERSION`.

## Test

All test targets run from `src/keur`.

```bash
make test              # unit + integration + e2e (the full suite)
make test-unit         # pure functions only, fast (doctest suite "unit")
make test-integration  # real rule engine over fixtures (doctest suite "integration")
make e2e               # end-to-end: real binary against real git repos
make perf              # performance timing guards (doctest suite "perf")
```

Unit and integration tests live in `keur_test.cpp` and are organised into
doctest suites (`--test-suite=unit`, `--test-suite=integration`,
`--test-suite=perf`). The e2e tests are shell scripts under `tests/e2e/`
(`test_check.sh`, `test_scan.sh`, `test_hook.sh`, `test_policy.sh`,
`test_version.sh`, `test_selfcheck.sh`), driven by `tests/e2e/run-e2e.sh`.

The test build injects `KEUR_TEST_ROOT` so tests can locate repo fixtures.

### Testability model

Subcommands take their I/O through injectable adapters, so tests exercise every
branch without a real git repo, real files, or real process spawns:

- `keur check` accepts a `FileSource` callable. Tests pass a lambda returning a
  fixed file list. The decision itself lives in `policy_evaluate`, a pure
  function that maps a `PolicyInput` struct to a `Verdict`, so it is
  table-testable across every combination of inputs.
- `keur hook` takes a `HookDeps` bundle (`Git`, `Fs`, `Proc`). Tests assign
  fakes to any member.

## Coverage

```bash
make coverage
```

Builds an instrumented binary with `--coverage -O0`, runs the test suites
(excluding `perf`), and prints per-file line coverage via gcov for the keur
sources (`check`, `hook`, `scan`, `policy`, `git`, `fs`, `proc`, `report`). Raw
gcov data lands in `.tmp/cov/`. For an HTML report, run `lcov --capture` on that
directory.

## Mutation testing

```bash
make mutate
```

Runs mull over an instrumented build. Requires `mull-runner` (or
`mull-runner-18`) and the mull IR frontend clang plugin. The target checks for
both and prints install guidance if either is missing. See
[mull.readthedocs.io](https://mull.readthedocs.io).

## Layout

The binary is organised in layers. Each layer maps 1:1 onto a C++ namespace and
onto a directory under `src/keur`.

```text
src/
  rules/              shared rule engine (ADR-001)
    rule_engine.h     Rule / RulePattern / RuleTarget / RuleFinding + API
    rule_engine.cpp   RE2-backed loading, matching, scanning
  keur/
    main.cpp          argv dispatch table -> cmd::*
    cmd/              subcommand orchestrators   (namespace keur::cmd)
      check.{h,cpp}   staged diff -> rule engine -> policy -> verdict
      scan.{h,cpp}    directory scan -> findings -> report
      hook.{h,cpp}    hook install / global / status / remove
      policy.{h,cpp}  config resolution + policy explain
    adapters/         the ONLY place external I/O happens (namespace keur::adapters)
      git.{h,cpp}     all git queries (run, staged_files, hooks dir)
      fs.{h,cpp}      filesystem mutations + existence checks
      proc.{h,cpp}    external process spawning
    report/           findings output provider (namespace keur::report)
      report.{h,cpp}  finding JSON, JSONL / compact / verbose, colour
    util/
      keur_utils.h    shared helpers (jesc, rule_dirs)
    keur_test.cpp     doctest unit + integration tests
  Makefile            builds bin/keur, links rule_engine.o
vendor/
  doctest.h           single-header test framework
```

Principles (see ADR-003):

- One implementation per external concern. One git spawn site, one findings
  emitter, one TOML reader.
- Adapters are structs of `std::function` members defaulted to the real
  implementation. Production uses the default; tests inject fakes.
- Decision logic stays pure. I/O stays at the edge in the adapters.

## Code style — the way forward

Small, testable pieces over clever density. These are conventions, not dogma —
performance is a valid reason to deviate, but say so in a comment.

- **Small, single-purpose functions.** One function, one job. A function you can
  name precisely is a function you can test in isolation. Prefer extracting over
  a growing body.
- **Dispatch by the right mechanism (ADR-014).** Don't reach for a long
  `if/else`/`switch` on a type or key when the branch set grows: use a lookup
  table for data/uniform branches, a strategy registry for behaviour/extension
  points. Keep short, closed chains (e.g. CLI-flag parsing) as-is — the rule is
  fit, not "no if/else". `KEUR-QUAL-001` nudges on 8+ `else if (x == …)`.
- **Name conditions, don't inline them (ADR-015, parked).** `if (age >= 18 &&
  !banned && verified)` → `const bool isEligible = …; if (isEligible)`. Reads
  better and the condition is testable without entering the flow. Central,
  business-critical conditions can live in one place.
- **Positive boolean names.** `isValid`, `hasAccess` — never `isNotInvalid`.
  Prefix booleans with `is/has/can/should`.
- **Query vs. command (CQS).** A function either returns a value (query, no side
  effects: `get/fetch/find`) or performs a side effect (command). Don't mix.
- **I/O at the edge (ADR-003).** Decision logic stays pure; side effects live in
  `adapters/`. Pure logic is the testable core.
- **Files stay small.** Aim well under ~300 net lines per file and a handful of
  files per directory; split by responsibility when it grows. The limit *is* the
  complexity check — no separate analysis needed.

Rationale in `AGENTS.md` (the short version for humans and AI agents) and the
ADRs (the why).

## Self-enforcing rules (keur guards itself)

keur runs its own rule engine over its own source. The architecture is a
control, not just a convention. The rules live in `rules/keur-internal/`:

- **KEUR-ARCH-001** (`KEUR-ARCH-001-io-via-adapter.rule`, `block`): fires on raw
  `popen`, `system`, `mkdir`, or `unlink` anywhere outside `adapters/` and the
  tests. External I/O outside the adapter layer fails the gate.
- **KEUR-ARCH-002** (`block`): every file under `adapters/` must declare
  `namespace keur::adapters`.
- **KEUR-ARCH-003** (`block`): every file under `cmd/` must declare
  `namespace keur::cmd`.
- **KEUR-ARCH-004** (`block`): every file under `report/` must declare
  `namespace keur::report`.

`tests/e2e/test_selfcheck.sh` runs KEUR-ARCH-001 over `src/keur` on every test
run, asserts zero findings, and then injects a temporary violation to prove the
control actually fires. That guards against a dead rule.

Isolating I/O in `adapters/` is what lets KEUR-ARCH-001 stay a clean one-liner
(`exclude_paths: adapters/ ...`) instead of a fragile per-file list. When adding
code, keep I/O in the adapters and keep each layer in its own namespace so the
self-check passes.

## Writing rules

Rules are `.rule` files in the cpm-compatible format, grouped by category under
`rules/`. Supported engines: `pattern`, `presence`, `absence`. A rule declares
its `id`, `title`, `category`, `severity`, `engine`, optional `action` and
`confidence`, a `target` (extensions / filenames / exclude_paths /
content_contains), and one or more `patterns` with a `regex` and a `message`.

Run your rules with `keur scan <path>` or point `KEUR_RULES_DIRS` at a directory
of them. See `rules/security/KEUR-SEC-001-private-key.rule` for a worked example.

> **Test a new or changed rule against the repo's `rules/` dir, not the
> installed snapshot.** A plain `keur scan` resolves rules from the *installed*
> locations (`$KEUR_HOME/rules`, `~/.config/keur/rules`, project `.keur/rules`),
> which can lag behind the repo and apply the maturity/difficulty filter. If a
> rule "doesn't fire", that is usually the cause, not the rule. Pin the engine
> to this repo's rules explicitly and use the standalone `keur-rules` binary,
> which does no difficulty filtering:
>
> ```bash
> cd src/rules && make                         # build bin/keur-rules
> KEUR_RULES_DIRS=$PWD/../../rules \
>   bin/keur-rules --files path/to/fixture.ts  # exact findings, repo rules only
> ```
>
> Always verify both directions: a **true positive** on the insecure fixture and
> **no false positive** on a secure counterpart (helmet present, limit set, …).

## Commits

Use Conventional Commits: `type(scope): description`, where scope is the
provider or area (for example `feat(check): ...`, `fix(hook): ...`,
`docs(adr): ...`). Do not commit build artifacts (`bin/`, `.tmp/`).

## Architecture decisions

- [ADR-001 - Native RE2 rule engine](docs/adr/ADR-001-native-rule-engine.md)
- [ADR-002 - keur as a single compiled C++ binary](docs/adr/ADR-002-single-cpp-binary.md)
- [ADR-003 - Provider / adapter architecture](docs/adr/ADR-003-provider-architecture.md)
