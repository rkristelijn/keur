# ADR-009 — Composite rules & the fact-driven engine (engine stays dumb, rules carry the logic)

@id ADR-009
**Status**: proposed · **Date**: 2026-09-19 · **Coverage**: code · **Relates to**: ADR-001 (native rule engine), ADR-004 (analysis-engine evolution — this is a concrete Stage 5), ADR-007 (pattern-level `exclude`)

## Context

KEUR's single detectors are strong but flat: one `.rule` = one condition (a regex,
a presence/absence, a block). That flat model cannot express the way a developer
actually reasons about a finding:

> "This is TypeScript **and** it uses NestJS **and** the pinned version is `< X`
> **and** middleware-bypass CVE-Y applies here → **error: upgrade or add this check**.
> But the same pattern in a *different* framework, or a patched version, is fine."

A single regex has no notion of *language*, *framework*, or *version*, so today that
reasoning either (a) can't be written, or (b) gets crammed into one over-specific regex
that overfits (the dominant reject reason in the learn loop: `overfit-regex`).

Three forces make this worth a decision now:

1. **Real security relevance is conditional and compositional.** A weakness matters
   *given a context*: stack, framework, version, and whether hardening is present.
   `lang=ts AND framework=strapi AND misconfig → warn`;
   `lang=ts AND framework=express AND version<V AND cve-Y → error`.
2. **Noise reduction by scope.** CVEs worth checking are the ones on code people
   actually run — recent CVEs against the framework/plugin **versions currently pinned**.
   A rule set scoped to "this stack, recent CVEs" is small, current, high-signal.
   The unbounded "every CVE ever" set is neither buildable nor useful in a pre-commit hook.
3. **Most-discriminating-first is a performance requirement.** No Java in the repo →
   never load Java rules. No Dockerfile → never run Docker checks. The cheapest filter
   ("what stack is this?") must run first and prune the rest.

The invariant we must not break while adding this (confirmed in the current build:
the engine is ~1113 lines across 9 files with **zero** hardcoded rule IDs, categories,
or security patterns — verified by grep):

> **Business logic lives in the rules and the folder structure. The engine is small
> and purely executive.** `struct Rule { id; metadata; expression; }`, not a class
> hierarchy (ADR-004). The engine knows *how* to match; the rules know *what* is
> suspicious.

The risk is obvious: a composite/fact model is exactly where an engine grows a brain —
a config language, a version-constraint solver, a dependency resolver. That would move
logic *into* the engine and betray the invariant. This ADR exists to add the capability
**without** that drift.

## Decision

Introduce two additive, data-first concepts. Both keep the engine dumb: they are new
*data* the engine evaluates, not new *knowledge* the engine holds.

### 1. Facts — a cheap, first-class context pass

@id ADR-009.facts

Before matching, one light pass establishes per-target **facts**: a small, flat,
language-neutral map the rules can join on.

```text
facts = {
  langs:      [ts, go, ...]          # from file extensions present
  frameworks: [express, nestjs, ...] # from manifest deps + import markers
  versions:   { express: "4.17.1", nestjs: "9.0.0", ... }  # DIRECT manifest pins only
  runtimes:   { node: "18", python: "3.11", ... }          # engines / base-image tags
}
```

Facts are derived from **direct manifest pins and file presence only** — `package.json`,
`go.mod`, `pom.xml`, Dockerfile `FROM`, CI `uses:`. They are cheap, deterministic, and
require no network and no dependency graph. (See Scope & limits: transitive resolution
is explicitly out.)

### 2. `requires:` — rules and composites join on facts

@id ADR-009.requires

A rule (single or composite) may declare a `requires:` guard. If the facts don't
satisfy it, the rule is **never loaded or run** — this is the most-discriminating-first
prune, expressed as data.

```yaml
id: KEUR-JS-EXPRESS-CVE-Y
requires:
  lang: ts
  framework: express
  version: { express: "<4.18.0" }
engine: composite
all:
  - ref: express-vuln-sink-pattern      # an ordinary single rule, reusable on its own
  - not:
      ref: express-mitigation-present   # bypassed if the guard is already applied
action: block
message: "Express <4.18.0 with CVE-Y sink and no mitigation — upgrade or add the guard here."
```

Semantics, kept minimal and RE2-friendly:

- `engine: composite` combines the results of **other rules** with a boolean tree:
  `all` / `any` / `not` (and later `xor` if a real case demands it — not speculatively).
- Each leaf is a `ref:` to a rule that **also runs standalone**. A composite composes
  existing evidence; it does not introduce a new match primitive. (This is why the
  engine stays small: composites are evaluated *after* the normal pass, over findings
  already produced.)
- `requires:` is a pre-filter over `facts`. Version predicates are simple comparisons
  (`<`, `<=`, `>=`, `range`) against a **directly pinned** version string. No solver.
- A composite with an unsatisfiable `requires:` is silently skipped (like a rule for a
  language not present) — never an error.

This is the "dev brain" as data: facts are what you notice first; `requires:` is the
"does this even apply here?" gate; the `all/any/not` tree is the reasoning; each leaf
is a check that stands on its own.

### 3. The engine change is small and bounded

@id ADR-009.composite

- A **facts pass**: read the manifests present, emit the flat `facts` map. New, but tiny
  and adapter-shaped (ADR-003) so parsers stay swappable.
- A **`requires:` filter** in the existing dispatch (`rule/scan.cpp` already indexes by
  extension/filename — this extends that prune with facts).
- A **composite evaluator**: after the normal single-rule pass, resolve `ref`s and
  evaluate the boolean tree over the findings map. ~100–200 lines, no new match engine.

Everything else — which languages, which frameworks, which CVEs, which combinations —
is **data** in `rules/` and its folder structure. The engine gains an evaluator, not a
brain.

## Why not the alternatives

- **Put the composite logic in C++** (a framework/CVE table in the engine): fast to hack,
  but it moves business logic into the engine and needs a recompile per new CVE. Betrays
  the ADR-001/004 invariant and kills the "teams grow rules, nobody touches the engine"
  property. Rejected.
- **One big over-specific regex per case** (encode framework+version in the pattern):
  this is what the learn loop already rejects as `overfit-regex`. It doesn't generalise
  and it can't express "unless mitigated". Rejected.
- **A full config/DSL language** (loops, variables, functions): that is a compiler project
  (the exact trap ADR-004 names). `all/any/not` over `ref`s + a flat facts join is the
  smallest thing that expresses the need. Anything richer waits for a concrete case.
- **A dependency/version solver** (resolve transitive versions to decide applicability):
  that is SCA — a different product, network-bound, and graph-heavy. Out of scope; we
  read direct pins only and accept the resulting precision limit (below).

## Scope & limits

- **Facts are shallow by design.** Direct manifest pins + file presence + import markers.
  No transitive dependency resolution, no lockfile graph walk. A version predicate on an
  indirect dependency is out of scope — that is SCA, not KEUR.
- **Precision limit, stated honestly.** If a framework/version can't be read from a direct
  pin, the `requires:` guard is treated as *not satisfied* → the composite doesn't fire.
  We prefer a missed conditional finding over a guessed one (silence over noise).
- **CVE scope is a policy, not an engine feature.** "Recent CVEs for the pinned stack" is
  how the *rule set* is grown (the learn loop + a CVE feed), not something the engine knows.
  The engine just runs the rules it's given.
- **Composites compose existing rules.** A `ref` must resolve to a real rule; a composite
  introduces no new line/block match primitive. Cross-file composition follows the same
  `(repo,file,category)` granularity the rest of the engine uses.
- **Backwards compatible.** Rules without `requires:`/`composite` behave exactly as today.

## Consequences

- KEUR can finally express conditional, framework-aware, version-aware findings the way a
  developer reasons — as **data**, with the engine still dumb and small.
- Most-discriminating-first becomes real: the facts pass prunes whole rule families before
  any matching, so a JS-only repo never pays for Java/Docker/Terraform rules.
- The composite model is the concrete realisation of **ADR-004 Stage 5** (`all/any/not`),
  reached additively — no big-bang, no new engine, the RE2 line core untouched.
- New CVEs / frameworks / stacks arrive as **new `.rule` files in a directory** (the
  `KEUR_RULES_DIRS` cascade), pulled and updated locally with no recompile — companies
  and teams grow their rule set; the engine stays frozen. This is the "build once, let it
  scale" property, preserved.
- Honest limit carried forward from ADR-004: KEUR reproduces *patterns and their
  composition*, conditioned on shallow facts — never interprocedural taint or a dependency
  graph. The line between "more than Semgrep in breadth/speed/self-improvement" and "never
  in depth" stays bright.
