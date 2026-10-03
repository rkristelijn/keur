# ADR-034: Regex-safety pre-flight gate for LLM-proposed rules (ReDoS defense)

## Status

Accepted (2026-09-26)

## Context

The learning loop (cold-start `iterate` and seed `seed-iterate`) executes
regexes **proposed by a local LLM** against the whole corpus to measure them.
Executing untrusted, machine-generated regexes is a denial-of-service surface:
a pattern with catastrophic backtracking (ReDoS) — nested quantifiers `(a+)+`,
overlapping alternation under a quantifier `(a|a)+`, stacked unbounded wildcards
`.*.*.*`, or an enormous bounded repetition `a{100000}` — can hang or pin CPU
for seconds to minutes on a single input, multiplied over hundreds of repos.

### What is and isn't protected

- keur's **own engine is RE2** (`src/rules/rule_engine.h`: `#include <re2/re2.h>`,
  "keeps the engine single, RE2, linear"). RE2 has no backtracking, so it is
  immune to catastrophic backtracking. The gate *measurement* itself cannot hang.
- **But** the loop also passes each candidate through **Python `re`**
  (`regex_generalises`), which is a backtracking engine and *is* vulnerable.
  Future validation paths or engines could add more exposure.
- A pattern RE2 cannot compile makes the gate measure a silent no-op (the rule
  loads as dead), wasting an iteration.

So even though the production engine is safe, the learning pipeline around it
executes LLM output through unsafe paths. That warrants an explicit gate.

## Decision

Add `regex_safe()` in `optimise.sh`, run as a **pre-flight** on every candidate
**before** `regex_generalises` and before any corpus execution, in both the
cold-start loop and the seed refine loop. A candidate that fails is rejected
(cold start) or fed back to the LLM as a retry ("rewrite with linear
constructs"), exactly like the overfit guard.

`regex_safe` refuses, for every `regex:` and `exclude:` in the candidate:

1. **Nested quantifiers** — `(x+)+`, `(x*)*`, `(x+){2,}`.
2. **Alternation under a quantifier** — `(a|a)+`, `(a|ab)*`.
3. **Stacked unbounded wildcards** — `.*.*.*`.
4. **Adjacent unbounded classes** — `[..]+[..]+`.
5. **Oversized bounded repetition** — `{n}` / `{n,m}` with n or m > 1000.
6. **Non-compiling patterns** — anything Python `re` (and by extension RE2)
   rejects.

Verified with unit tests: all five danger families + a non-compiling pattern are
flagged UNSAFE; the real recovered near-misses (`verify=False`,
`rejectUnauthorized:false`, `YAML.load(`) are correctly SAFE.

## Consequences

- The loop cannot DoS itself on its own LLM output; unsafe regexes never reach
  execution.
- The reject reason `redos-unsafe` is logged in `attempts.tsv`, distinct from
  `overfit-regex` and `gate`, so the trace stays diagnosable.
- Heuristic, not a proof: the danger list covers the common evil-regex families,
  not every possible ReDoS. It is a cheap, high-value filter, layered before the
  existing guards — defense in depth, not a guarantee.

## Follow-up: promote to standalone keur rules

The same detection should become first-class keur rules (e.g. `SEC-REDOS-001..`)
so keur flags catastrophic-backtracking regexes **in real repositories**, not
just in its own learning loop. That is a separate change (rule authoring +
corpus validation) tracked as follow-up; `regex_safe` is the loop-internal
version of the same logic and can seed those rules.

## Alternatives considered

- **Rely on RE2 immunity alone**: rejected — the loop demonstrably runs
  candidates through Python `re` too, and non-compiling patterns still waste
  iterations.
- **Time-box each measurement instead of pre-filtering**: rejected as the
  primary control (a timeout wastes the work and does not teach the LLM), but is
  a reasonable complementary backstop for a future hardening pass.
- **Run candidates in a sandboxed subprocess with a CPU limit**: heavier; the
  static pre-flight covers the known families at near-zero cost and gives the
  LLM actionable feedback, so it is the first line.
