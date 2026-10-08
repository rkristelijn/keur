# ADR-056 — Orchestrate clang-tidy for semantic checks RE2 cannot do (unused includes first)

@id ADR-056
**Status**: accepted · **Date**: 2026-10-08 · **Coverage**: code (`make lint-includes` + CI dogfood step) · **Relates to**: ADR-001 (RE2 per-line engine — the constraint), ADR-019 (external-tool scanners & project-local adapter — the orchestration model), ADR-040 (engine coverage gaps — what the engines cannot catch), ADR-050 (CI stage ordering — where lint runs)

## Context

Some checks need **symbol resolution**, not pattern matching: "is this `#include`
actually used?" is only answerable after resolving which symbols the TU
references and which header provides each. keur's rule engine is RE2, per-line,
no semantics (ADR-001) — it cannot do this without heuristics that misfire on
macros, transitive includes, and type-only use. The same wall we hit with the
4A assert-after check (ADR-055): a regex can approximate, not decide.

This came up concretely while splitting `keur_test.cpp` into per-suite TUs: each
split file inherited the monolith's broad include list, leaving many `#include`s
unused. Spotting them by eye (or by grep) is exactly the unreliable path to
avoid.

The right tool exists and is proven: **clang-tidy** `misc-include-cleaner`
(and `include-what-you-use`). keur already has a scanner-orchestration model
(ADR-019): it shells out to gitleaks/semgrep/trivy and normalises their output
rather than reimplementing them. Unused-include analysis belongs in that same
"orchestrate a proven tool" lane, not in a `.rule`.

## Decision

Do **not** write a keur rule for unused includes. Instead orchestrate
clang-tidy:

@id ADR-056.target

- A `make lint-includes` target runs `clang-tidy --checks='-*,misc-include-cleaner'`
  over the target TUs with the project's real include flags, greps the
  "included header … is not used directly" diagnostics, and fails (exit 1) if
  any remain. It resolves a real clang-tidy even when Homebrew keeps it off PATH
  (`/opt/homebrew/opt/llvm/bin/clang-tidy`).
- It is wired into the **dogfood** CI job (ADR-050) as its own named step, with
  `clang-tidy` added to the apt install.
- Scope is the test TUs today; `cmd/` and `config/` carry pre-existing
  unused-include debt the check surfaced, so widening `TIDY_TUS` to
  `main.cpp + $(IMPL_SRCS)` waits until that debt is cleared (follow-up).

This generalises: **any check that needs a compiler front-end (unused includes,
unnecessary copies, modernize-*) is a clang-tidy orchestration, not a rule.**
Rules stay for the grep-able, language-agnostic, sub-second-hook cases; the
semantic analysis is delegated to the tool built for it (ADR-019/040).

## Consequences

**Positive**
- Reliable unused-include detection (symbol-resolved, not guessed), kept green
  by CI so the split files do not silently re-accumulate dead includes.
- Reuses the established orchestration lane (ADR-019); no fragile regex rule to
  maintain or explain-away false positives on.
- Opens a clear path for a sibling `lint-perf` (clang-tidy `performance-*`) with
  the same shape.

**Negative / risk**
- CI now needs clang-tidy installed (apt one-liner; already added). Local runs
  degrade gracefully — the target skips with a note if no clang-tidy is found,
  so a contributor without LLVM is not blocked.
- `misc-include-cleaner` also flags *missing* direct includes (symbols used only
  transitively). Useful, but noisier; the target greps only the "not used
  directly" line for the gate and leaves the missing-include diagnostics as
  advisory output.
- Pre-existing `cmd/`/`config/` debt means the target cannot cover the whole
  tree yet without failing CI on unrelated code — hence the scoped `TIDY_TUS`.

## Follow-up

- Clear the `cmd/`/`config/` unused-include debt, then widen `TIDY_TUS` to the
  whole build and drop the scoping comment.
- Add `make lint-perf` (clang-tidy `performance-*`) in the same shape for the
  common C++ performance smells (unnecessary copies, value params, `std::endl`,
  for-range copy) — the statically-detectable half of performance work;
  profiling (perf/valgrind/Instruments) stays out of scope for a lint gate.
