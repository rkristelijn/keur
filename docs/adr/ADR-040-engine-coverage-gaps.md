# ADR-040 — Engine coverage gaps: what keur's rule engines can and cannot catch

@id ADR-040

## Status

Proposed · 2026-10-03 · Coverage: none
· Relates to: ADR-001 (native rule engine), ADR-004 (analysis-engine evolution),
ADR-014 (engine registry), ADR-009 (rules are data),
`docs/design/scope.md` (guard, not a Sonar clone),
`rules/keur-internal/KEUR-CPP-001-lambda-all-capture.rule` (a gap we DID close)

## Context

Running SonarCloud on keur's own PR surfaced a spread of findings. We adopted the
"fix + control" discipline: for each finding keur *should* own, write a rule so
keur catches it itself. Doing that exposed a precise question — **which finding
classes can keur's current engines express, and which need a new engine?**

keur has 14 engines today: `pattern`, `presence`, `absence`, `multiline`,
`block-absence`, `max-nesting`, `count`, `extract-duplicates`, `max-lines`,
`max-dir-files`, `path-derived`, `file-presence`, `file-absence`, `composite`.

## Decision

Record the coverage map and decide, per gap, whether to build an engine or leave
it to the analyzer (cpm/Sonar). The guiding line is `scope.md`: keur is a
deterministic commit **guard**, not a maintainability analyzer. An engine is
worth building only when the finding class is (a) deterministic and (b) worth
blocking/warning at commit time.

### Covered by existing engines (no gap — just write the rule)

Most Sonar findings are single-line or signature-level and the `pattern` engine
handles them. Examples from the PR, all expressible as `pattern`:

- lambda all-capture `[&]`/`[=]` → **done**, KEUR-CPP-001.
- `open(sys.argv[..])` path traversal → **done**, PY-SEC-013 extension.
- `const std::string&` → `string_view`, `printf` → `std::print`, raw-string
  literal, structured-binding, `std::function`→template: all regex-matchable on
  the declaration/call site. (Rules not yet written; no engine gap.)

### Real engine gaps (a pattern cannot express these)

Three finding classes need structural understanding the engines lack. All three
share a root cause: they reason about **non-brace code structure**.

1. **Keyword-delimited blocks** — e.g. a shell `case … esac` with no `*)`
   default arm, or `if … fi` without `else`. `block-absence` is **brace-scoped**
   (`{}`); it cannot see `case/esac`, `do/done`, `if/fi`. A `block-absence`
   attempt on `case` was tried and **removed** (it silently never fired — worse
   than no rule). **Gap: a keyword-scoped block engine** (open-token/close-token
   pairs, required-inner regex). Deterministic and guard-worthy (a missing
   default arm hides unhandled input) → **worth building.**

2. **Misleading indentation** ("this statement will not be executed
   conditionally") — a statement visually indented as if guarded by an `if` but
   actually unconditional. Needs an **indentation-aware** engine comparing the
   leading whitespace of consecutive statements. No engine models indentation.
   Deterministic and a real bug class → **worth building (smaller, bounded).**

3. **Cognitive complexity over a threshold** — needs the full
   branch/nesting-weighted metric per function. `max-nesting` covers *depth*
   only, not the Sonar cognitive-complexity formula. Borderline for a guard:
   complexity is a maturity concern (cpm/Sonar), not a "stop dangerous code"
   concern. → **Leave to the analyzer** unless a cheap depth proxy
   (`max-nesting`) is deemed enough; do not reimplement Sonar's metric.

### Explicitly out of scope (scope.md: not a Sonar clone)

The modern-C++ nudges (string_view, std::print, emplace vs push_back, raw
strings, structured bindings) are maintainability smell, not danger. keur may
carry them as **advisory** rules (pattern-matchable), but they must never block,
and reimplementing Sonar's full C++ ruleset is out of scope. Note the direct
conflict: Sonar says `push_back`→`emplace_back`, while keur's own KEUR-PORT-007
says the opposite for constructor-less aggregates on libc++ — keur is right for
*that* case, which is exactly why keur keeps its own, context-aware rules rather
than mirroring Sonar.

## Consequences

- **Positive**: the coverage map is explicit; "can keur catch X?" is answerable
  from the engine list, not guessed per finding. Two bounded engines
  (keyword-block, indentation) would close the deterministic gaps; everything
  else is either a rule-authoring task or deliberately the analyzer's job.
- **Negative / cost**: two new engines to design + register (ADR-014). Each is a
  per-line/҂window lexer, modest, but adds engine surface to maintain.
- The honest limit is recorded: not every Sonar finding is a good keur rule.
  Guard-worthy + deterministic is the bar; maturity smell stays advisory or with
  cpm.

## Follow-ups

1. `keyword-block` engine: open/close token pair + required-inner regex (covers
   shell `case`/`if`, and other `esac`/`fi`/`done` scopes). Then re-add the
   shell "no default arm" rule on top of it.
2. `indentation` engine (bounded): flag a statement whose indent implies it is
   guarded by a preceding `if`/`for`/`while` header but which is outside the
   block.
3. Decide `max-nesting` as the complexity proxy vs leaving complexity to cpm;
   do not clone Sonar's cognitive-complexity metric.
4. Author the advisory pattern rules for the modern-C++ nudges (opt-in, never
   blocking).

## Composite candidates (leaf + leaf → sharp finding)

The lambda case proved the pattern: two weak leaf rules AND-ed into a precise
composite approximate Sonar's data-flow verdict, deterministically, as DATA
(no smarter engine). Built here: KEUR-CPP-001 (all-capture) + KEUR-CPP-002
(stored) → KEUR-CPP-003 (stored all-capture = likely dangling). More families in
the corpus are the same shape — follow-ups, each is just a composite over
existing leaves:

- **WEB-SEC**: CORS wildcard origin + `credentials: true` together = exploitable
  (either alone is weaker). Composite over the two existing leaves.
- **IAC-SEC**: `latest` image tag + no pinned digest; or privileged + hostPath
  mount — unhardened combinations beyond the existing root+healthcheck
  (KEUR-IAC-010).
- **SH-QUAL**: no `set -e` + pipe without `pipefail` = silent pipeline failure.
- **KEUR-CPP**: extend with raw-`new` + no RAII owner, etc.

Principle: keep leaves broad+quiet (advisory), let the composite be the sharp,
higher-confidence signal. Build them after the current PR lands, not inside it.
