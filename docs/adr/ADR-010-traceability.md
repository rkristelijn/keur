# ADR-010 — annotrace: lightweight traceability (`@id` / `@implements` / `@proves`)

@id ADR-010
@id annotrace
**Status**: accepted · **Date**: 2026-09-19 · **Coverage**: code · **Relates to**: ADR-002 (single binary — why no external tool), ADR-004 & ADR-009 (large ADRs that use sub-`@id`s)

## Name

The convention has a name: **annotrace** — *annotation-driven traceability*. One
word for the whole idea: links between decisions, code, and tests are carried by
explicit `@`-annotations (`@id` / `@implements` / `@proves` / `@see` /
`@external`) rather than inferred, and those annotations trace a concept across
artefacts. When this repo (or an adopter) says "annotate it for annotrace" or
"does annotrace cover this?", it means exactly the tag-set and coverage contract
below. The concept itself declares `@id annotrace`, so it is traceable by its own
rules.

## Context

keur has decisions (ADRs), code, and tests, but no way to *measure* whether every
decision is actually built and tested. We wanted bidirectional traceability — the
ISO/IEC/IEEE 29148 idea, as tools like OpenFastTrace implement it — so we can
answer "is this decision built? is this behaviour tested?" and find the gaps.

Constraints that shaped the choice:

- **No external tool.** OpenFastTrace is a Java suite; a JAR in the pre-commit
  path breaks the single-binary rule (ADR-002). We want the OFT *model*, not its
  runtime.
- **Refactor-proof.** A link must survive moving code and renaming files. So it
  points at a **stable concept ID**, never `file:line` or a heading number.
- **Language- and how-agnostic, fast.** Measuring must be sub-second and work
  across any language, by reading tags — never by building or running tests.

An earlier attempt inferred links by matching loose `ADR-NNN` text; it produced
false positives (a `cpm ADR-170` prose mention, a path fragment). The lesson: a
reference must be an **explicit tag**, and an ID must be **explicitly declared** —
no form heuristics.

## Decision

Traceability is carried by explicit, grep-able tags. One flat namespace of
concepts; the tooling knows only "declared" vs "referenced", never the type.

- **`@id <token>`** — declares a concept anywhere (any markdown/code): `ADR-009`,
  `RFC-003`, `principle-least-privilege`. Must be unique. Large documents declare
  a sub-`@id` per independently-buildable part (`@id ADR-004.stage4`); never link
  to a heading number/anchor (that is a position and rots on reorder).
  Rules declare via their `id:` field; ADRs may use their filename.
- **`@implements <id>`** — this code/rule builds that concept (concept→code).
- **`@proves <id>`** — this test proves that concept works (concept→test). It is
  **how-independent**: the tag counts wherever it lives, and the TEST TYPE follows
  from the location — `*_test.cpp` = unit/integration, `tests/e2e/*.sh` =
  e2e/smoke, `.rule` = regression/self-check.
- **`@see <token|path|url|text>`** — weak "see also" (Doxygen sense), unvalidated
  unless its first token is a declared `@id`.
- **`@external <repo>:<id>`** — a deliberate other-repo reference (`cpm:ADR-170`);
  recorded, never validated against keur's own IDs.

Two axes are measured, and they are **different questions**:

- **concept → test** (`@proves`): does the behaviour work? (a runtime test)
- **concept → doc** (spec has a section): is it still described? (a static
  consistency check, not a test)

`eval/doc-coverage.sh` reports both plus `@id` uniqueness, dead links, and
per-part status of large ADRs. `--strict` makes it a gate.

The full convention lives in `docs/design/traceability.md`; this ADR is the
decision, that doc is the reference.

## Why not the alternatives

- **OpenFastTrace (the tool)**: a JVM dependency in the hook — rejected per
  ADR-002. We take its model (`@id`/covers/proves, coverage report), not its JAR.
- **`file:line` links**: rot on the first refactor. Rejected for stable IDs.
- **Inferring links from prose** (`ADR-NNN` anywhere): false positives from paths
  and cross-repo mentions. Rejected for explicit tags + explicit `@id`.
- **A heavy RTM tool / external matrix**: disproportionate to keur's size (10
  ADRs). grep over tags is enough and stays in-repo.

## Scope & limits

- **Explicit only.** Nothing is inferred; a link is a tag a human wrote.
- **Seeding is a judgement.** Upgrading a `@see` to `@implements` is semantic, so
  it is proposed (by a human or the AI prompt in `docs/prompts/`), never
  auto-claimed.
- **The report is shell; the gate belongs in the engine.** The percentage report
  is set arithmetic (`doc-coverage.sh`, shell). The commit-gate checks — dead link
  (`@implements X` with no `@id X`) and duplicate `@id` — are findings over code
  and will be implemented as `.rule` files in the RE2 engine, so anyone adopting
  annotrace gets them in `keur check` without copying a script. (Rules TODO —
  reserved family `KEUR-TRACE-*`; the `KEUR-DOC-*` ids are taken by the ADR-061
  doc-standards family and must not be reused here.)

## Acceptable gaps (what the gate tolerates)

Not every gap breaks the build — a gate that demands 100% gets disabled. Split by
*errors* (broken existing annotations — never acceptable) vs *incompleteness*
(deliberate "not yet" — acceptable while visible).

| Gap | Verdict | Why |
|-----|---------|-----|
| Dead link (`@implements ADR-042`, no such id) | **BLOCK** | broken reference, a correctness error |
| Duplicate `@id` | **BLOCK** | two concepts sharing an identity breaks the model |
| `@proves x` with no passing test | **BLOCK** | a claim not backed by a green test lies to the meter |
| Orphan ADR (`Coverage: code`, no `@implements` yet) | **WARN** | decided-but-not-built is normal; must stay visible |
| Roadmap sub-`@id` (`ADR-004.stage4` not built) | **WARN** | it is literally a roadmap |
| Untested command / engine | **WARN** | tolerated briefly; the weakest — untested behaviour is risk |

Rule: an incompleteness gap is acceptable **only while it is visible in the
report** — never silently. The block-tier is what `KEUR-DOC-001/002` enforce; the
warn-tier is reported and can be forced to zero in CI with `--strict`. This mirrors
keur's own policy: findings are evidence, some block, some warn.

## TODO — clickable navigation (`keur trace --fix`)

An `@id`/`@implements`/`@proves` names a concept but is not clickable in an
editor. A follow-up capability: a `--fix`-style generator that appends (and keeps
updated) a resolvable link next to each tag — e.g. `@implements ADR-009`
becomes `@implements ADR-009 (docs/adr/ADR-009-*.md#L3)` — so in VS Code you can
ctrl-click straight to the declaration. Requirements: the link is *generated*
(never hand-maintained), regenerated when files move (so it stays refactor-proof
— the `@id` remains the source of truth, the link is a derived convenience), and
it must never become the thing the meter reads (the tag stays canonical). This is
a navigation aid layered on top, not part of the coverage contract.

## Consequences

- Every decision, and each part of a large one, is traceable to the code that
  builds it and the test that proves it — measurable, sub-second, any language.
- The convention is portable: teams adopting keur annotate with the same four
  verbs and get the same coverage report and gate.
- keur keeps guarding itself: the planned `KEUR-TRACE-*` rules will fail the build
  on a dead link or a duplicate `@id`, the same way `KEUR-PORT-*` guards platform
  drift. Until then `doc-coverage.sh --strict` is the annotrace gate.
- Honest limit: coverage measures that a *claim exists* (a `@proves` tag), backed
  by the discipline that the test must actually pass (verified in `make test`) —
  the tag never substitutes for a green test.
