# ADR-021 — `--debug` / `--trace`: engine observability & feature-liveness

@id ADR-021
**Status**: proposed · **Date**: 2026-09-21 · **Coverage**: none · **Relates to**: ADR-001 (RE2 engine — the thing we observe), ADR-003 (I/O via adapters — the trace sink is one), ADR-014 (engine registry — each engine is a traceable unit), ADR-020 (path-derived engine — debugging it is what surfaced this need)

## Context

Two recurring problems have no first-class answer today:

1. **Debugging the engine means editing source.** When a rule silently produces
   no findings (e.g. the ADR-020 path-derived rule during development), the only
   way to see *why* — did the engine run? what did it derive? did the path
   match? — was to add a temporary `fprintf` and recompile. That leaves debug
   cruft on a hot path and is invisible to a user in the field.

2. **"Zero findings" doesn't prove a feature ran.** `keur scan` reporting no
   findings can mean "the code is clean" **or** "the rule/engine never actually
   executed" (a dead rule, a mis-scoped target, a broken load filter). Our
   self-check guards one rule (`test_selfcheck.sh` injects a violation to prove
   KEUR-ARCH-001 fires), but there is no general way to assert in an e2e test
   that a given engine or rule *executed at all*. As we add engines (count,
   multiline, path-derived, …) this liveness gap grows.

Both are observability needs. The fix is one facility, not scattered prints.

## Decision

Add two levels of runtime observability, off by default (zero cost when unset),
emitted through an injectable **trace sink** (an adapter, so it is spoofable and
tests can capture it without a terminal — ADR-003):

- **`--debug`** (or `KEUR_DEBUG=1`): human-readable diagnostics to stderr —
  which config resolved, how many rules loaded, per-file which rules were
  candidates, and *why* a rule did/didn't fire. For a person understanding a
  surprising verdict.
- **`--trace`** (or `KEUR_TRACE=1`): machine-readable JSONL to stderr (or a
  file) — one event per engine execution: `{file, rule_id, engine, matched,
  reason}`. For tests and tooling to assert behaviour.

stdout stays clean (findings/verdict only), so piping is unaffected — the same
discipline `scan` already follows (findings to stdout, counts to stderr).

### The trace sink (shape)

```cpp
// A no-op by default; --debug/--trace install a real sink. Injected like the
// other adapters so pure logic stays pure and tests capture events in memory.
struct Trace {
  bool debug = false, trace = false;
  std::function<void(std::string_view human)> note;              // --debug
  std::function<void(const TraceEvent& ev)> event;               // --trace (JSONL)
};
struct TraceEvent { std::string file, rule_id, engine, reason; bool matched; };
```

Engines emit at most one event each per file via `cx.trace.event({...})`. The
`ScanContext` gains a `const Trace&` member; when tracing is off, `note`/`event`
are empty and the calls are a cheap branch — no formatting cost. This is the
only change to the engine signature; engines otherwise stay the small,
registered functions of ADR-014.

### Feature-liveness in e2e

`--trace` makes liveness assertable. A new e2e check runs the suite's fixtures
with `--trace`, collects the set of engines that emitted at least one event, and
asserts every registered engine (from the ADR-014 registry) appears. A rule or
engine that never runs across the whole fixture corpus is a dead feature and
fails the check — the general version of what `test_selfcheck.sh` does for one
rule.

## Why not the alternatives

- **Ad-hoc `fprintf` / `#ifdef DEBUG`**: what we have informally; leaves cruft,
  needs recompiles, not available to users, not captureable by tests. The thing
  this ADR removes.
- **A logging library**: pulls a dependency against the single-binary /
  sub-second-startup constraints (ADR-002); overkill for structured line output
  we can emit ourselves (same reasoning as writing our own JSONL elsewhere).
- **Always-on verbose output**: pollutes stdout, breaks piping, costs formatting
  on the hot path. Off-by-default with a cheap guard is the correct trade.
- **A separate `keur explain <rule>` command**: useful later, but static; it
  can't show what happened on a *specific* scan. Complementary, not a
  substitute for runtime trace.

## Consequences

- Debugging a rule/engine is `keur scan --debug <path>` — no recompile, no
  source edits, available to users and to us. Core stays free of stray prints.
- E2e gains a feature-liveness gate: every engine must demonstrably execute in
  the fixture suite, closing the dead-rule/dead-engine gap generally.
- One new adapter (Trace) + one `ScanContext` field + flag/env parsing. Engines
  gain one optional emit line each. Requires a `@proves` test (a spoofed Trace
  captures events) and a `docs/rules-authoring.md` note on reading `--debug`.
- Boundary held: sink is an injected adapter, output is our own JSONL, tracing
  is off by default — still one fast binary, still I/O at the edge.
