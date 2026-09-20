# ADR-014 — Engine strategy-registry & the dispatch decision rule

@id ADR-014
**Status**: proposed · **Date**: 2026-09-19 · **Coverage**: code · **Relates to**: ADR-001 (the RE2 line engine), ADR-002 (single binary — why compile-time, not runtime plugins), ADR-004 (staged engine growth), ADR-009 (facts/composite land as plugins on this)

## Context

The scanner dispatches on engine type with a growing `if (engine == "pattern")
… else if (engine == "presence") …` chain in `scan.cpp` (6 branches, each real
behaviour). Every new engine (facts, requires, composite, count, …) adds another
branch, so the scan loop keeps changing — the opposite of "minimal impact on the
rest of the code". A long conditional on a type is the classic signal to
*replace conditional with polymorphism / a table*.

Two forces:

1. **Engines are an extension point.** The whole direction (ADR-004/009) is
   adding engines. They should attach without touching the dispatch loop.
2. **Not every conditional is that.** CLI-flag parsing (`--tier`, `--format`) is
   also an `if/else` chain, but it is trivial, closed, and local — turning it
   into a registry would be over-engineering. We need a *rule* for which is
   which, or people (and AIs) will abstract the wrong things.

Constraint from ADR-002: keur is one compiled binary with sub-second startup.
So "plugin" here means **compile-time strategy objects in a registry**, never
runtime-loaded `.so`/`.dll` — no dynamic loading, no ABI, no startup cost.

## Decision

### 1. The dispatch decision rule

Choose the dispatch mechanism by two axes: **how much behaviour per branch** ×
**does the branch set grow with the domain**.

| Signal | `if/else` (or `switch`) | lookup table (`map<key,fn>`) | strategy registry (interface + objects) |
|--------|------------------------|------------------------------|------------------------------------------|
| behaviour per branch | trivial (set a field, toggle a flag) | data / uniform (key → set field) | substantial (own logic/state) |
| branch set grows with the domain | no | yes | yes |
| extension point for others | no | sometimes | yes |
| example in keur | CLI-flag parsing | `parse.cpp` field dispatch | `scan.cpp` engine dispatch |

Read it as: **list doesn't grow → `if`. Grows but each branch is data → table.
Grows and each branch is real behaviour → registry.** Not "no `if/else`" — a
short, closed, trivial chain is the honest, most readable choice and must stay.

### 2. Engines become a strategy registry

Each engine implements a small interface and registers by name; the scan loop
asks the registry instead of branching:

```cpp
struct Engine {
  virtual ~Engine() = default;
  virtual std::string_view name() const = 0;                 // "pattern", "count", …
  virtual void eval(const Rule&, const ScanContext&,
                    std::vector<RuleFinding>&) const = 0;
};
// registry: name → Engine*, populated at startup. scan.cpp: registry.at(engine).eval(...)
```

Adding an engine = one new file implementing `Engine` + one `register(...)`.
The scan loop does not change. This is the compile-time "plugin" model — minimal
impact, still one binary, still sub-second.

### 3. `parse.cpp` field dispatch becomes a lookup table

The 17-branch `key == "id" … else if key == "title"` chain is data-uniform and
grows per rule-field → a `map<string, setter>`. New field = one entry.

### 4. `count` — the first engine built on the registry

A general engine: **flag when a regex matches ≥ `min_count` times within a
scope.** It takes a normal `patterns[].regex` (yes — it "slikt" a regex, same as
every other engine) plus:

- `min_count: <N>` — fire only at N or more matches.
- `within: file` — scope (start with per-file; per-block later).

It is reusable far beyond one control: long dispatch chains, too many imports,
too many TODOs, an over-long function. Its first rule, `KEUR-QUAL-001`, enforces
*this ADR's own decision rule* — 8+ `else if (x == …)` branches warn "consider a
table or registry", making the rule self-guarding (like `KEUR-PORT-*`).

## Why not the alternatives

- **Runtime plugins (`.so`/`.dll`)**: dynamic loading breaks the single-binary
  guarantee (ADR-002), adds startup cost and an ABI surface. Rejected.
- **Leave the `if/else` chain**: every new engine keeps editing the scan loop —
  the coupling we want gone.
- **Registry for everything (incl. CLI flags)**: over-abstraction; hides three
  lines behind an interface. The decision rule exists precisely to prevent this.

## Consequences

- New engines (facts, requires, composite, count) attach as registry entries;
  the scan loop is closed to modification, open to extension.
- The decision rule is explicit, so refactors don't "clean up" the good, short
  `if/else` chains, and don't leave the real dispatch smells.
- `count` gives keur a self-guarding control for its own decision rule, and a
  reusable "too many of X" primitive.
- Migration is test-first: characterise the 6 engines (done), refactor to the
  registry with behaviour identical (tests stay green), then add `count`.
- Boundary held: engines are strategy objects in the binary, not loadable
  modules. keur stays one fast artifact.
