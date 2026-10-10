# ADR-060 — Declarative stage-pipeline engine: compose fixed verbs (extract | count | classify) as data

@id ADR-060

## Status

Proposed · 2026-10-10 · Coverage: none
· Relates to: ADR-009 (composite = boolean tree over findings; this is the
*data-transformation* sibling, and inherits its "engine stays simple" invariant),
ADR-002 (single binary, no runtime plugins — stages are compiled primitives,
not shelled-out tools), ADR-014 (engine registry), ADR-058 (first pipeline:
literal classification), ADR-059 (generic rules — the pipeline's class table
lives there)

## Context

The literal-classification work (ADR-058) is naturally a **pipeline**:
`get all code → filter(code, comments) → group-by-amount → classify`. The user
asked whether a rule could express that as data — a `compose: extract | count |
classify` — rather than one monolithic engine or (rejected, ADR-002) an external
language shelled in.

keur already has `engine: composite` (ADR-009), but it is the wrong shape: it is
a **boolean tree over the findings of other rules** ("A and B fired → escalate").
It combines *results*; it does not pass *data* from one stage into the next.
A transformation pipeline — where the output of `extract` is the input of
`count`, whose output is the input of `classify` — is a different primitive that
does not exist yet.

The attraction is the Unix-pipe mental model: small, composable, each stage does
one thing. The danger is the exact one ADR-009 names — *"a composite/fact model
is where an engine grows a brain."* A free-form pipeline in data (arbitrary
steps, conditionals, loops) is a programming language in YAML, which both ADR-009
and the ADR-002 "no external language" stance reject.

## Decision

Add a **declarative stage-pipeline engine** (`engine: pipeline`) whose power is
deliberately bounded: a rule rings together a **closed, named set of stage verbs**
that the C++ engine implements. The rule chooses *which* verbs, in what *order*,
with what *parameters* — it cannot define a new verb in data.

```
id: literal-classify
engine: pipeline
generic: true                 # ADR-059: feeds nothing standalone
stages:
  - extract: literals         # tokenizer third projection (ADR-058)
  - count: unique             # group-by-amount
  - classify: { table: literal-classes }   # → generic rule's class table (ADR-059)
report: by-bucket             # how to emit (noise dropped, magic/identity shown)
```

Each stage is a compiled C++ primitive with a fixed contract: a typed input, a
typed output, declared parameters. The engine runs them left to right, feeding
each stage's output to the next. The *composition* is data; the *stages* are code.

### The closed verb set (initial)

| Verb | In → Out | Params | ADR |
|------|----------|--------|-----|
| `extract: literals` | file → literals[] (text, line) | — | 058 |
| `count: unique` | literals[] → (literal, count)[] | — | 058 |
| `classify` | (literal, count)[] → (literal, count, class, bucket)[] | `table:` (generic rule id) | 058/059 |

New verbs are added **only** as a C++ primitive + an ADR amendment — never as
data. This is the hard line that keeps the engine simple: data may *sequence* verbs,
never *define* behaviour.

### What this is NOT

- **Not conditionals/loops/variables in data.** No `if`, no `for`, no branching.
  A pipeline is a straight left-to-right sequence of fixed verbs. The moment a
  rule needs a branch, that is a new verb (C++ + ADR), not a data construct.
- **Not an external language** (ADR-002). Stages are in-process C++; no fork,
  no runtime dependency.
- **Not a replacement for `composite`.** Composite stays the boolean-over-findings
  tool; pipeline is the data-transformation tool. Different jobs.

## Impact on existing rules (the user's concern)

**None.** `engine: pipeline` is additive. Every existing rule keeps its current
engine (`pattern`, `composite`, `absence`, …) and is untouched — no re-review of
the rule corpus is required. The pipeline engine is a new entry in the registry
(ADR-014) that only rules opting into `engine: pipeline` use. The first and only
initial user is the generic literal-classify rule (ADR-058/059).

## Why not fold this into ADR-058

ADR-058 is *what* (find/classify literals). This is *how stages compose* — a
general engine mechanism that will outlive the literal use case (any future
extract→transform→report flow reuses it). Keeping it separate means the pipeline
engine can gain verbs and users without churning the literal ADR, and the
"closed verb set" invariant has one clear home.

## Consequences

- **Positive**: the Unix-pipe model the user wants, as data, with native speed
  and zero new runtime dependencies. Pipelines are readable, versioned, and
  (via ADR-059) tunable at repo/global level without a compile.
- **Positive**: additive — no existing rule changes, no corpus re-review.
- **Positive**: the closed verb set makes the pipeline auditable: you can list
  every verb the engine supports; a rule cannot smuggle in behaviour.
- **Negative / cost**: a new engine + a typed stage contract + per-verb
  implementations and tests. Start with exactly the three verbs ADR-058 needs;
  resist adding verbs speculatively.
- **Risk (named, bounded)**: scope creep toward a general compute engine. Guard
  with the invariant — a new verb requires a C++ primitive **and** an ADR line;
  data never defines behaviour, only sequences it. If a feature wants a
  conditional, that is the signal it is a new verb, not a data feature.

## Follow-ups

1. `engine: pipeline` registered in `rule/engines.cpp`; a typed Stage contract
   (input/output kinds, params) so verbs compose only where types line up.
2. Implement the three ADR-058 verbs (`extract: literals`, `count: unique`,
   `classify`) as stage primitives; unit-test each in isolation (one in → one
   out) and the composed pipeline end to end.
3. Parser: accept `stages:` (ordered list of verb+params) and `report:`;
   validate that each verb is known and that adjacent stage types match; reject
   an unknown verb with a clear "verb X is not a pipeline primitive (see ADR-060)".
4. Wire the generic literal-classify rule (ADR-059) as the first pipeline;
   e2e-test against this repo + security-tooling (ids, domains, case, magic
   words surface in the right buckets).
5. Document in rules-authoring: the closed verb set, the "sequence not compute"
   invariant, and "a new verb = C++ primitive + ADR, never data."
