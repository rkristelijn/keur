# keur chapter contract — fixed documentation headings per document, per CMMI level

The one source of truth for *which exact headings* keur expects in each root
documentation file, and *from which CMMI level* each becomes required. This is a
**contract**: each chapter has a canonical heading and a *closed, agreed set of
accepted aliases* — not free synonym-matching. A finite agreed vocabulary is
what makes a doc reliably machine-parseable (purposeful-readme) and lets a
human, an agent, and keur all agree on where a fact lives (ADR-061).

> **Why a closed alias set, not free synonyms, and not one rigid word?**
> One rigid word ("Context" only) is unrealistic — teams legitimately write
> "Deployment", "CI/CD" or "Pipeline" for the release chapter. Free synonym
> tolerance is the opposite failure: it accepts anything and detects nothing, so
> drift hides. The middle is a **closed alias set per concept**: keur accepts
> `Release | Deployment | CI/CD | Pipeline | Publishing` for the release chapter
> because those are agreed *here*, but rejects an unlisted word. Want a new
> alias? Add it to this contract (one place), never ad-hoc per repo.

Rules: headings are H2 (`## `) unless noted; order is the order below; a level
includes every heading from the levels beneath it (cumulative). keur enforces
presence per heading via `KEUR-DOC-005` (README) and `KEUR-DOC-01x` (the others);
the required set per file is gated by the `maturity` field on each rule.

## README.md — the six RFC chapters (canonical heading + closed alias set)

Each chapter has one **canonical heading** and, where the industry genuinely
uses interchangeable words, a **closed set of accepted aliases**. keur accepts
any heading in the set; it does *not* accept arbitrary synonyms. A new alias is
added *here* (one place), never ad-hoc per repo. This buys realistic adoption
(teams already write "Deployment" or "CI/CD" for the release chapter) without
losing detectability (the set is finite and agreed).

| Order | Canonical heading | Accepted aliases (closed set) | Required from | Notes |
|-------|-------------------|-------------------------------|---------------|-------|
| 1 | `## Context` | Overview, About, Introduction | CMMI-1 | what & why; link the business case |
| 2 | `## Prerequisites` | Requirements, Dependencies | CMMI-2 | versions/runtimes/services — reference, don't re-document |
| 3 | `## Local setup` | Getting started, Install, Installation, Usage, Quick start | CMMI-1 | product: run it; library: use it |
| 4 | `## Validate` | Validating, Testing, Tests, Running tests | CMMI-2 | how to run the checks that prove a change is safe |
| 5 | `## Release` | Deployment, Deploy, CI/CD, Pipeline, Publishing | CMMI-2 | how changes ship (tag? pipeline? merge-to-main?) |
| 6 | `## More info` | Further reading, References, Resources, Documentation, Links | CMMI-2 | links to ADRs/Confluence/docs, who to contact |

## Repo type — declared in config, never guessed

The required chapter set depends on *what the repo is*. A CLI utility's README
should not demand a "Release/deploy to prod" chapter; a service's must. keur
does **not** guess the type from repo contents — guessing is a weak label
(ADR-032), gives unpredictable verdicts, and fights keur's "same input, same
verdict" core. Instead: **declare it, with a safe default when unset.**

**Declared in `.config/keur.toml`** (ADR-011, a fifth axis beside
lifecycle/criticality/maturity):

```toml
[project]
type = "util"   # app | service | library | util | monorepo | both
```

**Default when the field is absent: `util`** — deliberately the *least
demanding* type, so a repo without a profile is nudged for the baseline only
(Context, Prerequisites, Local setup, More info) and never ambushed with
Validate/Release eisen it may not owe. A repo opts *into* stricter type-specific
chapters by declaring `app`/`service`; it is never silently held to them.

This mirrors `KEUR-CFG-001/002` (a repo should declare its own policy, not rely
on per-machine defaults) and ADR-011's "required is policy": the type is a
business decision the maintainer states once, in one reviewable place. No
heuristics, no "why did this fire".

### Required README chapters per type

| Chapter | app | service | library | util | monorepo |
|---------|-----|---------|---------|------|----------|
| Context | ✓ | ✓ | ✓ | ✓ | ✓ |
| Prerequisites | ✓ | ✓ | ✓ | ✓ | ✓ |
| Local setup | ✓ (run it) | ✓ (run it) | ✓ (use/import it) | ✓ (install/use it) | ✓ (per-package pointer) |
| Validate | ✓ | ✓ | — | — | ✓ |
| Release | ✓ | ✓ | ✓ (semver/publish) | — (install.sh is the release) | ✓ |
| More info | ✓ | ✓ | ✓ | ✓ | ✓ |

Validate and Release are *contributor* concerns for a `util`/`library` — they
move to `CONTRIBUTING.md`, and the README links to it. keur itself declares
`type = "util"`, so its README omits Validate/Release (they live in CONTRIBUTING).
A `monorepo` root README orients and points at per-package READMEs (each of which
carries its own type). The default (`util`) is why an unconfigured repo passes
with just the four baseline chapters.

## AGENTS.md — fixed headings (agents.md popular set, pinned)

| Order | Fixed heading | Required from |
|-------|---------------|---------------|
| 1 | `## Project overview` | CMMI-2 |
| 2 | `## Setup` | CMMI-2 |
| 3 | `## Build and test` | CMMI-2 |
| 4 | `## Code style` | CMMI-3 |
| 5 | `## PR instructions` | CMMI-3 |

## CONTRIBUTING.md — fixed headings

| Order | Fixed heading | Required from |
|-------|---------------|---------------|
| 1 | `## Prerequisites` | CMMI-3 |
| 2 | `## Build` | CMMI-3 |
| 3 | `## Test` | CMMI-3 |
| 4 | `## Code style` | CMMI-3 |
| 5 | `## Commits` | CMMI-3 |
| 6 | `## Architecture decisions` | CMMI-3 |

## SECURITY.md — fixed headings

| Order | Fixed heading | Required from |
|-------|---------------|---------------|
| 1 | `## Reporting a vulnerability` | CMMI-3 |
| 2 | `## Scope` | CMMI-3 |
| 3 | `## Supported versions` | CMMI-3 |

## CHANGELOG.md — Keep a Changelog (fixed category headings)

Section headings per release are the fixed Keep-a-Changelog set (used as `###`):
`### Added`, `### Changed`, `### Deprecated`, `### Removed`, `### Fixed`,
`### Security`. Required from CMMI-3. Not every release uses every category —
keur only checks that the file *uses the vocabulary*, never invents its own.

## How keur enforces it

Each fixed heading above maps to one pattern in a `KEUR-DOC-*` presence/absence
rule. The rule's `maturity` field = the "Required from" column, so the same
binary expects fewer headings on a prototype and the full set on a mature repo
(ADR-011). A missing required heading is a `warn` (block at CMMI-4 under
`--strict`, ADR-037). The self-explaining message names the exact heading to add
(ADR-035) — no guessing which synonym keur wants, because there are no synonyms.
