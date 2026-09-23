# ADR-016 — Repository layout, test placement & doc coverage per maturity (parked)

@id ADR-016
**Status**: proposed (parked) · **Date**: 2026-09-19 · **Coverage**: none · **Relates to**: ADR-003 (layers = namespaces = directories), ADR-010 (traceability coverage), ADR-011 (maturity/lifecycle axes that these grow with), ADR-013 (parked backlog) · **Prior art**: cpm adr-026 (V-model enforcement), cpm check-comment-ratio. @external cpm:adr-026

<!-- Coverage: none — decision recorded, not yet built/enforced. Makes the
     implicit layout + test-placement conventions explicit and consistent, and
     defines doc/comment coverage that grows with maturity (ADR-011). -->

## Context

Several conventions are currently *implicit* — we follow them but never decided
them, so a fresh session (human or AI) sees the layout without the reasoning, and
some of it is inconsistent. Concretely:

- **Test placement is mixed.** Unit/integration tests are colocated with the code
  (`src/keur/keur_test.cpp`), but e2e tests live apart (`tests/e2e/*.sh`). Nobody
  decided this; it just grew. It must be *consistent and reasoned*.
- **Folder structure** (`src/ docs/ tests/ rules/ eval/ config/ scanners/
  scripts/ rules-imported/`) has no stated rule for "where does a new thing go".
- **Docs live inside the code** (Doxygen-style headers, `@id`/`@implements`), not
  in separate doc files next to each source — that too is a choice worth stating.
- **No doc/comment coverage that grows with maturity.** A prototype needs little;
  an MVP needs technical + functional docs; higher maturity needs more. Today
  there is no rule tying documentation depth to the ADR-011 maturity/lifecycle.

cpm already solved much of this (don't reinvent):

- **cpm adr-026 — V-model enforcement**: checks mapped to maturity levels. Level
  0 "just ship it" → Level 3 full V-model. You only get enforced what your level
  requires. This is exactly "docs/tests grow with maturity".
- **cpm V-model**: technical side (unit + integration tests) on the left arm,
  functional side (acceptance / e2e tests) on the right arm — validating against
  design and requirements respectively.
- **cpm check-comment-ratio**: a comment-ratio threshold (default 20%) over src/.

## Decision (direction — parked, not yet enforced)

### 1. Test placement — consistent, reasoned

Adopt cpm's V-model split as the *rule*, not an accident:

- **Technical tests (unit + integration) — colocated with the code.** They test
  units/modules against their design, so they live next to the code
  (`*_test.cpp` beside the module). Colocation *encourages writing them* and keeps
  them refactor-adjacent. This is the modern norm (Go/Rust/TS).
- **Functional tests (acceptance / e2e) — separate, in `tests/e2e/`.** They test
  the whole system against requirements, span modules, and are language-agnostic
  shell — so they belong in one system-level place, not beside any single module.

Rationale for the split (answers "does coverage invite separating tests?"): a
separate tree can ease coverage tooling but *invites forgetting tests* (out of
sight). Colocated unit tests keep the incentive to write them; only system-level
e2e — which has no single home in the code — goes in `tests/`. So the mix is
correct; this ADR makes it the *rule*, not a happenstance.

### 2. Repository layout — the rule for "where does it go"

| Directory | Holds | Rule |
|-----------|-------|------|
| `src/` | the binary; layers = namespaces (ADR-003) | new code goes in its layer's dir |
| `tests/e2e/` | system/functional shell tests | e2e only; unit tests stay colocated |
| `docs/adr/` | decisions | one ADR per decision |
| `docs/design/` | concepts/vision | cross-cutting design |
| `rules/` | keur's own + adopted rules (by category); origin in a `source:` field | shipped rules |
| `eval/` | the learn loop + meters (shell) | training/measurement, not shipped in the binary |
| `config/` | default `keur.toml` | documented defaults |
| `scanners/`, `scripts/` | helper shims | small glue |

Docs live *in the code* (Doxygen headers + `@id`/`@implements`), not as parallel
files — one source of truth, refactor-proof (ADR-010).

### 3. Doc/comment coverage that grows with maturity (ADR-011)

Tie documentation depth to the project's maturity/lifecycle, V-model style:

| Lifecycle / maturity | Documentation required |
|----------------------|------------------------|
| `pilot` (PoC) | *what the PoC must prove*, and — once proven — the core. Minimal how. |
| `mvp` | + technical docs (how it is built) AND functional docs (what it does); technical tests (unit+integration) present |
| `business-ready` | + functional/acceptance (e2e) tests, comment-ratio floor (cf. cpm's 20%) |
| `production-ready` / `cmmi-4+` | + full V-model: requirements↔acceptance, design↔integration, all traced (ADR-010) |

Comment/doc coverage is measured, not guessed: a **comment-ratio control** (adopt
cpm's) plus the existing traceability coverage (ADR-010). Both grow with the
profile — `learn → guide → guard → enforce` (cpm's level ladder), so a pilot is
nudged and a live service is gated.

## Why not

- **All tests in a separate tree**: cleaner for coverage tooling, but moves unit
  tests out of sight and weakens the incentive to write them. Rejected for the
  colocated-unit / separate-e2e split.
- **Docs as parallel files next to code**: two sources of truth that drift.
  In-code docs + `@id` traceability stay single-source and refactor-proof.
- **One fixed doc bar for all repos**: over-burdens a pilot, under-serves a live
  service. Coverage grows with maturity (ADR-011), like everything else.

## Consequences

- Layout and test placement become explicit rules a fresh session can read — the
  implicit knowledge from this session is captured, not lost.
- Doc/comment coverage joins the profile-driven model: measured, and growing with
  maturity, reusing cpm's V-model and comment-ratio as prior art.
- Parked: the comment-ratio control and the maturity→doc matrix are specified
  here but not yet built; they graduate into their own work when picked up
  (alongside ADR-011/012 enforcement).
