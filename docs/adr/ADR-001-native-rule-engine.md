# ADR-001 — Native RE2 rule engine

Status: accepted
Date: 2026-09-16

## Context

KEUR's rule engine must evaluate ~1000+ `.rule` files (2000+ regex directives)
against a diff or a repo, fast enough for a pre-commit hook (<~3s). An initial
shell implementation (per-rule / per-pattern process spawning) was correct but
took minutes on the full corpus: the cost is thousands of `awk`/`perl`/`rg`
process launches, not the matching itself. Shell orchestration that spawns a
process per item does not scale to this rule count.

The sibling project `cpm` already solved this with a compiled C++ engine
(`src/rules/rule_engine.cpp`) backed by RE2, scanning a repo in ~0.5s. Its
`.rule` file format is the same one KEUR adopted.

## Decision

KEUR gets its own **native, compiled rule engine** under `src/rules/`, following
cpm's proven pattern but written clean for KEUR:

- **RE2** for regex (Google's library): linear-time, no catastrophic
  backtracking (no ReDoS) — which matches KEUR's "deterministic guardrails"
  promise. Same engine as cpm, so the same patterns behave identically.
- **Single directory walk**, **single read per file**, **extension/filename
  index** for rule dispatch, **literal `content_contains` pre-filter**, and
  **pre-compiled** RE2 patterns — the performance features that make it fast.
- Engines implemented: `pattern`, `presence`, `absence` (covers ~99% of the
  corpus). Long-tail engines (`file-absence`, `file-presence`,
  `extract-duplicates`, `block-absence`) are out of scope for v1 and reported as
  unsupported rather than silently ignored.
- Output is KEUR's normalised **Finding JSON** on stdout (one object per line),
  so `scanners/rules.sh` becomes a thin wrapper the evidence/policy layers
  already expect.
- Inline suppression uses **`keur:ignore [<rule-id|category>]`** (KEUR's marker,
  not cpm's), same token-exact matching so `SEC-04` cannot suppress `SEC-043`.

## Consequences

- KEUR gains a build step (`make` in `src/rules/`) and a runtime dependency on
  RE2. `KEUR_NO_NATIVE=1` falls back to the shell engine for environments
  without a compiler/RE2.
- The `.rule` format stays cpm-compatible, so cpm's corpus can be reused via
  `KEUR_RULES_DIRS`.
- KEUR stays independent: its own engine, not a dependency on the cpm binary.
