# Documentation standards by CMMI level — a keur reference

Quick reference for *which* repository documentation is expected at each CMMI
maturity level, and *which keur control* enforces it. The decision and the
research behind this live in **ADR-061**; keur's `maturity` axis (CMMI 1–5) is
defined in **ADR-011**. This page is the scannable table, not the argument.

> **Scope note (from ADR-011):** CMMI describes an *organization's* process
> maturity. keur borrows the ladder as a **requested quality bar for a single
> repo** — not a claim about the org. The bar is a sliding scale, never a flat
> "every repo needs every file".

## The ladder (cumulative — each level includes everything below it)

| CMMI | Name | Doc bare-minimum | keur control | Verdict |
|------|------|------------------|--------------|---------|
| **1** | Initial | `README` with **Context** + **Local setup** | DOC-005 (two headings exist) | warn |
| **2** | Managed | + `LICENSE`, full 6-section `README`, `AGENTS.md` | DOC-001 presence, DOC-005 all 6 | warn |
| **3** | Defined | + `CONTRIBUTING`, `SECURITY`, `CHANGELOG`, SemVer; cross-links required | DOC-001/002/003 | warn, escalating |
| **4** | Quantitatively Managed | + `CODE_OF_CONDUCT`, doc-coverage measured & gated | DOC-002/003/005 flip warn→block (`--strict`) | block |
| **5** | Optimizing | + issue/PR templates, `CODEOWNERS`, drift-regression fixtures | all DOC-* block; a fixture per past drift | block |

## The six README sections (APS RFC; meaning shifts by repo type)

| # | Section | Product (maintainer reads) | Library/tool (consumer reads) |
|---|---------|-----------------------------|-------------------------------|
| 1 | Context | why this repo exists; business case | what problem it solves for you |
| 2 | Prerequisites | runtime + DB + services to *run* it | peer-deps / runtime to *use* it |
| 3 | Local setup | run the whole system locally | import it / call the API |
| 4 | Validate | run the test suite | usually n/a |
| 5 | Release | pipeline / tag / deploy | semver + changelog |
| 6 | More info | architecture, team, designs | API docs, examples, versions |

## The KEUR-DOC rule family (what each one finds)

| Rule | Finds | Engine | Status |
|------|-------|--------|--------|
| DOC-001 | **gap** — baseline doc missing (README/LICENSE/AGENTS) | `file-absence` | active (CMMI-2) |
| DOC-001b | **gap** — CMMI-3 doc missing (CONTRIBUTING/SECURITY/CHANGELOG) | `file-absence` | active (CMMI-3) |
| DOC-002 | **bad linking** — a doc has zero cross-links (started duplicating) | `absence` | active (CMMI-3) |
| DOC-003 | **drift / contradiction** — a command in README/AGENTS not in CONTRIBUTING | cross-reference | parked (needs ADR-022) |
| DOC-004 | **shim divergence** — a CLAUDE.md/.cursorrules that is a copy, not a pointer | `max-lines` | active (CMMI-2) |
| DOC-005 | **gap** — README missing an essential chapter (Context/Local setup) | `absence` | active (CMMI-1) |
| DOC-005b | **gap** — README missing a type-independent CMMI-2 chapter (Prerequisites/More info) | `absence` | active (CMMI-2) |
| DOC-005c | **gap** — README missing Validate/Release (app/service only) | `absence` | parked (needs project-type fact) |
| DOC-006 | **too-detailed → drift bait** — a hardcoded patch version in prose (`node 18.17.1`) | `pattern` | active (CMMI-2) |
| DOC-007 | **too-much / clutter** — README past the one-page budget | `max-lines` | active (CMMI-3) |
| DOC-008 | **not self-documenting** — an error string saying *what* failed, not *how to fix* | `pattern` + exclude | active (CMMI-3) |
| DOC-011 | **gap** — AGENTS.md missing an agreed chapter | `absence` | active (CMMI-2) |
| DOC-012 | **gap** — CONTRIBUTING.md missing an agreed chapter | `absence` | active (CMMI-3) |
| DOC-013 | **gap** — SECURITY.md missing an agreed chapter | `absence` | active (CMMI-3) |

Chapter headings and their closed alias sets live in
`docs/design/doc-chapter-contract.md`. The reasoning is in ADR-061. Parked rules
are authored as `.rule.parked` (inert) with their design-of-record intact, ready
to activate by renaming once the engine/fact they need exists.

**Can keur find "not self-documenting"?** Yes, as a negative heuristic (DOC-008,
warn-only): an error literal that names a bare failure ("invalid config") with no
actionable tail (no flag, path, "expected", "see <doc>") is flagged. It is fuzzy
by nature — a conversation starter, not a verdict (ADR-035).

**Overlap / duplication within a file** is additionally detectable with the
`extract-duplicates` engine (duplicate headings, repeated commands) — a candidate
for a future DOC rule.
