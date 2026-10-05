# Composite rules & facts — migration plan

Implements ADR-009. This is the *how* and the *order*; ADR-009 is the *what* and *why*.

Guiding rule for every step below: **the engine stays dumb, the logic stays in
`rules/` + folders.** If a step tempts you to put a framework list, a CVE table, or a
version rule *in C++*, stop — it belongs in data.

## Code-quality constraints (bind every step)

- **SRP — one file, one job.** New engine files mirror the existing shape:
  `facts.cpp` (derive facts), `composite.cpp` (evaluate the boolean tree). No file grows
  a second responsibility. If `scan.cpp` starts doing facts, split it.
- **Small clear moves, each shipping one product.** Every phase below is independently
  buildable, testable, and green before the next starts (same discipline as ADR-004/006).
  No phase leaves the tree half-migrated.
- **Short, logical names.** Folders and files stay short and obvious, matching the current
  tree (`rule/`, `cmd/`, `common/`; `scan.cpp`, `parse.cpp`, `match.cpp`). New:
  `rule/facts.cpp`, `rule/composite.cpp`. Rule data: `rules/facts/`, `rules/composite/`.
- **Data over code.** Frameworks, versions, CVEs, combinations = `.rule` files. The engine
  only ever gains an *evaluator*, never a *catalogue*.
- **Backwards compatible at every step.** Rules without `requires:`/`composite` behave
  exactly as today; existing tests stay green untouched.
- **Adapters at the seam (ADR-003).** Manifest parsing sits behind an adapter so the parser
  is swappable and unit-testable without the filesystem.

## Phases

### Phase 0 — baseline lock (no code)

- Snapshot current corpus metrics (`eval/report.sh`, `eval/measure.sh`) so every later
  phase is measured against a fixed baseline. A phase that doesn't hold or improve
  recall/precision on the corpus does not land.

### Phase 1 — facts pass (read-only, no behaviour change)

- New `rule/facts.cpp` + `rule/facts.h`: derive the flat `facts` map (langs, frameworks,
  versions, runtimes) from **direct manifest pins + file presence** only.
- Manifest readers behind an adapter: `package.json`, `go.mod`, `pom.xml`, Dockerfile
  `FROM`, CI `uses:`. Each reader is tiny and independently tested.
- Wire nothing into matching yet — expose facts via `keur scan --facts` (debug) so it can
  be verified in isolation.
- **Done when:** `--facts` prints correct langs/frameworks/versions for the corpus repos,
  with unit tests per reader. No change to any existing finding.

### Phase 2 — `requires:` prune (most-discriminating-first)

- Parse `requires:` in `rule/parse.cpp` (new optional field; absent = always applies).
- In `rule/scan.cpp` dispatch, drop rules whose `requires:` isn't satisfied by facts —
  extends the existing extension/filename index prune, doesn't replace it.
- Version predicate: simple compare (`<`, `<=`, `>=`, `range`) against a direct pin string.
  A tiny standalone comparator (`rule/semver.cpp`), unit-tested; no solver, no graph.
- **Done when:** a JS-only repo provably loads zero Java/Docker rules (assert in a test),
  corpus metrics unchanged for rules without `requires:`.

### Phase 3 — composite evaluator (`all/any/not`)

- New `rule/composite.cpp` + `.h`: after the normal single-rule pass, resolve `ref`s and
  evaluate the boolean tree over the findings already produced.
- Parse `engine: composite` + `all`/`any`/`not`/`ref` in `parse.cpp`. Each `ref` must
  resolve to a real rule (fail loudly at load if it doesn't — like an invalid regex).
- Leaves keep running standalone; the composite only *combines* their results.
- **Done when:** the ADR-009 Express example fires as one composite while its leaf rules
  still fire independently; unit tests cover `all`/`any`/`not` truth tables.

### Phase 4 — rule data + folders

- New rule homes with short names: `rules/facts/` (fact-detection helpers if any need to be
  rules), `rules/composite/` (the `all/any/not` rules), grouped by stack under logical
  subfolders (`rules/composite/js/`, `.../java/`, ...).
- Migrate any existing over-specific single rule that *wanted* to be conditional into a
  `requires:` + composite form. Delete the overfit version in the same commit.
- **Done when:** folder layout is self-explanatory, names are short, and `keur scan`
  produces the same-or-better corpus metrics.

### Phase 5 — learn loop + CVE feed (grow the data, not the engine)

- Point the learn loop at a CVE feed (NVD official API — no scraping, ADR/ SOURCES.md clean)
  scoped to **recent CVEs for the frameworks/versions the corpus pins**.
- The loop proposes `requires:`+composite rules; the deterministic gate still decides.
- **Done when:** at least one composite CVE rule is learned, gated, and promoted end-to-end.

## Self-check (keur guards itself)

- Add a `KEUR-ARCH-*` self-check rule that **blocks** hardcoded framework/CVE/version tables
  in `src/` — the same way `KEUR-PORT-*` blocks platform drift. This makes the ADR-009
  invariant enforceable, not just documented: business logic in `src/` fails the build.

## Order of value (if time is scarce)

1. Phase 1 (facts) + Phase 2 (`requires:` prune) — biggest correctness/perf win, smallest
   engine change, and immediately useful ("skip the Java meuk").
2. Phase 3 (composite) — unlocks the conditional findings.
3. Phases 4–5 — grow the data.
