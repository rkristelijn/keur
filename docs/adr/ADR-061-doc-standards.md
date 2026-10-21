# ADR-061 — Root doc-set: audience-indexed chapters, one source of truth per fact, cross-link not copy, and a deterministic drift guard

@id ADR-061

## Status

Proposed · 2026-10-12 · Coverage: none
· Relates to: ADR-010 (traceability — link to IDs, never duplicate), ADR-016
(layout & doc coverage per maturity — parked, same "docs are a control" line),
ADR-051 (stale-snapshot class of bug — a hand-maintained copy goes stale the
moment the source changes), ADR-009 (rules are data; this guard is `.rule`s,
not engine logic), ADR-035 (self-explaining findings — a drift finding must tell
a human *or an AI* exactly which file to fix), ADR-011/ADR-026 (project profile &
maturity — repo-type and the required-section bar are profile-driven; CMMI 1–5 is
the existing `maturity` axis this ADR gives a doc-column), ADR-037 (brief the repo
on the bar before gating it — warn precedes block), ADR-055 (defect-prevention:
a regression fixture per historical drift at CMMI-5)
· Grounds in: APS RFC "README.md context standardised for projects"
(Confluence PLAT/21744320552, ClickUp 861mgzzwv — co-authored by this maintainer):
the org-ratified six-section README standard, used here as the **human-layer**
section contract. External standards surveyed (see Research section): the
`AGENTS.md` open format (agents.md, Agentic AI Foundation / Linux Foundation),
Diátaxis (four doc types), standard-readme (RichardLitt), purposeful-readme
(AI-native, anti-drift), Keep a Changelog + SemVer, Contributor Covenant v2.1,
llms.txt (proposed/contested), and GitHub/GitLab community-health-file conventions.

## Context

A repo's root carries four audience-split instruction files:

| File | Audience | Function |
|------|----------|----------|
| `README.md` | humans (first contact) | what the project is, quick start, high-level map |
| `AGENTS.md` | AI coding agents | machine-readable, action-first: exact build/test commands, arch rules, errors to avoid |
| `CONTRIBUTING.md` | human contributors | social + technical rules: how to build/test/commit, PR etiquette, code of conduct |
| `SECURITY.md` | reporters | how to report a vulnerability privately, scope, supported versions |

`CONTRIBUTING.md` is the older de-facto standard (GitHub surfaced it on
issue/PR creation from ~2012); `AGENTS.md` is recent (~Aug 2025) and exists
*because* agents need terse, literal, non-negotiable facts where humans tolerate
prose and nuance. keur already ships all four and leans on them hard: `AGENTS.md`
is the short "way forward", `CONTRIBUTING.md` the long reasoning, `README.md` the
public face, `SECURITY.md` the disclosure contract.

### Audiences form a tree, and the entry doc differs per intent

"Humans vs. agents" is too coarse. A reader arrives with an *intent*, and that
intent — not the person — decides which doc is the right entry point. The tree:

```text
visitor to a repo
├── consumer — "I want to USE it, not change it"
│   ├── install a library/tool        → README (quick start, API, versions)
│   └── run the product locally        → README (quick start) → CONTRIBUTING (full setup)
└── contributor — "I want to CHANGE it"
    ├── human developer
    │   ├── build a feature            → CONTRIBUTING (setup, test, PR) + ADRs (why)
    │   ├── fix a bug                   → CONTRIBUTING (test harness, repro) + ADRs
    │   ├── submit a small patch        → CONTRIBUTING (commit/PR etiquette) — fast path
    │   └── report a vulnerability      → SECURITY (private disclosure, scope)
    └── AI coding agent (any of the above, on the human's behalf)
                                        → AGENTS (exact commands, arch rules, gotchas)
```

Two consequences fall out of the tree:

1. **The consumer and the contributor want different READMEs.** A consumer
   installing a library needs *how to call it*; a contributor needs *how to run
   and test the whole thing*. Cramming both into one README is the "too much
   clutter" anti-pattern the APS RFC names. keur's answer (below): README owns
   the consumer quick-start; the moment it turns into "how to develop it", that
   content moves to CONTRIBUTING and README *links* to it.
2. **The agent is orthogonal, not a fifth audience.** An agent may be fixing a
   bug or installing a dep; AGENTS.md is the *projection of every other doc into
   agent-readable form*, not a separate topic. That is why its facts must trace
   back to one owner rather than being authored fresh.

### Library README ≠ product README

The same six section headings mean different things by repo type, because the
reader's role flips from *developer-of* to *consumer-of*:

| Section | Software **product** (maintainer reads) | **Library/tool** (consumer reads) |
|---------|------------------------------------------|-----------------------------------|
| Context | why this repo exists; business case | what problem it solves *for you* |
| Prerequisites | runtime + DB + helper services to *run* it | peer-deps / runtime to *use* it |
| Local setup | run the whole system locally | import it / call the API (snippet) |
| Validate | run the test suite | usually n/a — you don't test *its* code |
| Release | pipeline, tag, deploy to DEV/UAT/prod | semver + how to read the changelog |
| More info | architecture, team, designs | API docs, examples, version matrix |

keur is deliberately **both**: a product (binary + hook that a maintainer
develops) *and* a tool consumers install (`brew`, `keur hook --install`,
ADR-045). So its README must serve the consumer quick-start while CONTRIBUTING
serves the maintainer — the exact split the audience tree predicts.

The failure mode is **documentation drift**: the build command changes, or an
ADR is added, and one of the four is forgotten. Humans and agents then get
*conflicting* instructions — the worst outcome, because an agent will follow the
stale literal command confidently. This is the same stale-snapshot bug class
ADR-051 names and the ADR-010 reason we link to `@id`s instead of copying text.

Three things could keep them in sync: (a) hand-discipline (fails — it is exactly
what drifts), (b) a doc-sync bot/CI that diffs prose against code (powerful but
heuristic, non-deterministic, and against the ADR-002 "no runtime brain" grain),
or (c) **a structural rule**: forbid the duplication that drifts, and verify the
cheap invariants deterministically. keur is a deterministic rule engine; (c) is
the native answer and keur should guard its own root doc-set the way it already
guards its own architecture (`KEUR-ARCH-*`).

## Decision

Adopt a **single-source-of-truth-per-fact** convention for the four root docs,
and enforce the mechanical parts of it with `.rule` files in
`rules/keur-internal/` (new `KEUR-DOC-*` family), the same self-guarding pattern
as `KEUR-ARCH-*`.

### 1. Ownership — each fact lives in exactly one file; the others link to it

- **Operational commands (build/test/coverage/mutation) live in `CONTRIBUTING.md`.**
  It is the deepest, human-owned technical reference and already holds them.
- **`AGENTS.md` carries the *short* action list and points at `CONTRIBUTING.md`
  and the ADRs for the why** — it must not re-document build internals that can
  drift from the Makefile.
- **`README.md` carries public context + quick start and points to
  `docs/design/vision.md` and the ADRs** — it is the one place a *user-facing*
  command (install, `keur hook --install`) may also appear, because its audience
  never reads `CONTRIBUTING.md`. That overlap is deliberate and bounded.
- **`SECURITY.md` owns the disclosure process**; the others only *link* to it,
  never restate it.

The rule, stated once: **repeat links, not facts.** If the same literal command
or threshold appears verbatim in two of the four, one of them is wrong by
construction.

### 1b. Chapters per file (the section contract each doc follows)

Each doc has a recommended chapter set. These are conventions (the ADR-015
"name the structure" instinct applied to docs), maturity-gated (ADR-011/026):
a prototype may ship a thin README; a mission-critical service is held to the
full set. **README** follows the APS RFC (human-layer, org-ratified); the others
follow their ecosystem norm.

**README.md** — six RFC sections, one page, staccato, consumer-first
(repo-type shifts their meaning per the table above):

```text
README.md
├── 1. Context         what & why (one sentence / small diagram; link the business case)
├── 2. Prerequisites   versions, runtimes, helper services (reference, don't re-document)
├── 3. Local setup     numbered install/run — product: run it; library: use it
├── 4. Validate        how to run the checks that prove a change is safe
├── 5. Release         how changes ship (tag? pipeline button? merge-to-main?)
└── 6. More info       Confluence/ADRs, docs/ folder, tooling, who to contact
    (anti-patterns: no clone/cd steps, no secrets, no manual-when-scriptable, POSIX paths)
```

**AGENTS.md** — the open format sets *no required fields*; these are the
*popular* sections (agents.md) kept terse and literal. Monorepos may nest an
AGENTS.md per package — the nearest file wins:

```text
AGENTS.md
├── Project overview        one line; link README, don't restate it
├── Setup / dev environment exact commands (link CONTRIBUTING as the owner)
├── Build & test commands   the literal gate the agent must run before finishing
├── Code style              the conventions ("way forward")
├── Testing instructions    how to run/scope tests; "fix failures before done"
├── Security considerations  gotchas; link SECURITY
└── PR / commit instructions  title format, Conventional Commits, run checks first
```

**CONTRIBUTING.md** — the owner of operational truth; the fast-path vs.
deep-path split matters (a patch author should not read the whole file):

```text
CONTRIBUTING.md
├── Prerequisites          the real toolchain (owner; README references this)
├── Build                  the authoritative build commands
├── Test / coverage / mutation  the full verification story (owner)
├── Code style             the long reasoning (AGENTS carries the short list)
├── Commits / PR etiquette  Conventional Commits, scope, no build artifacts
├── (link) Code of Conduct  → CODE_OF_CONDUCT.md, not restated here
└── Architecture decisions  → docs/adr/ (link, never duplicate)
```

**SECURITY.md** — the disclosure contract (GitHub surfaces it to enable the
"Report a vulnerability" flow); keur already ships this shape:

```text
SECURITY.md
├── Reporting a vulnerability  private channel, never a public issue/PR
├── What to include            repro, impact, affected version
├── Response                   acknowledgement, triage, coordinated disclosure
├── Scope                      what counts as a vuln in the tool vs. a finding it emits
└── Supported versions         which releases get fixes
```

### 2. The deterministic guard (`KEUR-DOC-*`, data not engine — ADR-009)

Add self-check rules that run over this repo's own root (file-level scope,
ADR-052):

- **KEUR-DOC-001 (`presence`, `warn`)** — each of the four files exists at the
  repo root. A missing `AGENTS.md`/`SECURITY.md` on a public repo is a gap.
- **KEUR-DOC-002 (`absence`/cross-reference, `warn`)** — `CONTRIBUTING.md`,
  `README.md`, and `AGENTS.md` each contain at least one link back into
  `docs/adr/` or to each other, so the "link don't copy" contract is live rather
  than aspirational. A doc with zero cross-links has probably started
  duplicating.
- **KEUR-DOC-003 (`cross-reference`, ADR-022, `warn`)** — build/test command
  tokens that appear in `AGENTS.md` or `README.md` (e.g. `make test`,
  `make coverage`) must also appear in `CONTRIBUTING.md` (set A ⊆ set B). If an
  agent-facing file names a command the human reference does not, they have
  drifted; the finding names both files.

- **KEUR-DOC-005 (`absence`, maturity-gated, `warn`)** — `README.md` carries the
  contract's chapter headings (closed alias set per chapter) appropriate to the
  repo type. The required set scales with the project profile (ADR-011/026): a
  prototype needs Context + Local setup; a mature repo needs the type-dependent
  rest. The repo-type (`app` | `service` | `library` | `util` | `monorepo`) is
  **declared in `.config/keur.toml`, never guessed** (guessing is a weak label —
  ADR-032 — and breaks "same input, same verdict"); it **defaults to `util`**
  (the least-demanding type) when unset, so an unconfigured repo is held only to
  the baseline, never ambushed. The type selects which chapters apply — a
  `util`/`library` moves Validate/Release to CONTRIBUTING; an `app`/`service`
  needs them in the README. keur declares `type = "util"`. See
  `docs/design/doc-chapter-contract.md`.

Each finding is self-explaining (ADR-035): it says *which* file duplicated
*which* fact and *where* the owning copy is, so the fix needs no detective work.
These are `warn`, not `block` — doc drift is a smell, not a secret.

### 3. Agent-tool compatibility shims by reference, never by copy

Tools look for their own file (`CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`).
Do **not** fork `AGENTS.md` into each. Point them at the one source:

- **Symlink** where the environment supports it: `ln -s AGENTS.md CLAUDE.md`.
- **One-line shim** where symlinks are not honoured (notably locked-down
  corporate endpoints — Intune/EPM-managed machines where symlinks are blocked):
  a stub file whose entire content is "See AGENTS.md" / an include directive.
  This keeps one editable source and avoids N copies that drift (the ADR-002
  "one artifact" instinct applied to docs).

A future `KEUR-DOC-004` may assert that any such shim is a link or a one-line
pointer, never a divergent copy — parked until a shim actually exists here.

### 4. keur's own README is a utility README (dog-fooding the split)

keur is `both` product and library (ADR-045 ships it via `install.sh` /
brew/npx), so by the library/product table its README must be **consumer-first**
and its build-from-source must live in `CONTRIBUTING.md`. Today keur's README
leads with "## Build" (clone, `brew install re2`, `cd src/keur && make`) — that
is *contributor* content; a consumer who just wants the hook never compiles it.
This is also why `KEUR-DOC-005` fires on keur's own README: it uses "Build"
where the contract expects "Local setup", the self-check honestly catching keur
breaking its own rule.

The fix, which keur adopts for itself:

- **README (consumer):** `## Context` (what keur is, the one-line pitch) →
  `## Prerequisites` (git; a POSIX shell) → `## Local setup` (install via
  `install.sh` / brew, then `keur hook --install`) → a short **demo** (a commit
  that gets BLOCKED, the one thing that sells it) → `## Release`/version →
  `## More info` (links to CONTRIBUTING, ADRs, vision). No compiler, no `make`.
- **CONTRIBUTING (contributor):** owns build-from-source — the `brew install re2
  abseil`, `cd src/keur && make`, the platform-layer note, CI. It already has
  `## Prerequisites` and `## Build`; the README's build block *moves* here (it is
  largely duplicated already), and README links to it.

This is the audience tree made concrete: a *consumer* reading README never meets
a build step; a *contributor* reading CONTRIBUTING gets the full toolchain. The
`install.sh` route (checksum-verified GitHub Release, ADR — no `curl | sh`) is
the consumer default and should be the README's headline, not an afterthought.

### What this is NOT

- **Not a prose-correctness checker.** The guard verifies *structural*
  invariants (existence, cross-links, command-token subset), not that the English
  is accurate. Semantic doc-sync (does this paragraph still describe the code?)
  is explicitly out of scope — it needs a brain keur deliberately does not have
  (ADR-002), and a heuristic bot can own that separately.
- **Not blocking.** `KEUR-DOC-*` warn. They nudge; they do not gate a commit.
- **Not new engine logic.** Reuses `presence`, `absence`, and `cross-reference`
  (ADR-022). New detection = a `.rule`, not C++ (ADR-009).

## Research: the documentation-standards landscape

Web research (Oct 2026) to ground the decision in existing standards rather than
invent a keur-local shape. Findings, with age, authority, and relevance.

### The documentation standards that exist

| Standard | Since | Authority / status | What it governs |
|----------|-------|---------------------|-----------------|
| **README** (as a convention) | ~1970s (Unix), GitHub-rendered from 2008 | de-facto; every platform renders it | project front page |
| **CONTRIBUTING.md** | ~2011, GitHub-surfaced 2012 | de-facto; linked on issue/PR | human contribution rules |
| **Keep a Changelog** | 2014 (Olivier Lacan) | de-facto; v1.1.0 | `CHANGELOG.md` — fixed categories Added/Changed/Deprecated/Removed/Fixed/Security, reverse-chronological, ISO-8601 dates |
| **Semantic Versioning (SemVer)** | 2013 (Tom Preston-Werner) | de-facto; v2.0.0 | version strings `MAJOR.MINOR.PATCH`; pairs with Keep a Changelog |
| **Contributor Covenant** | 2014 | de-facto; v2.1, Organization for Ethical Source | `CODE_OF_CONDUCT.md` text |
| **standard-readme** (RichardLitt) | 2017 | niche spec; library-focused | strict README section order (Title→…→License) |
| **Diátaxis** | 2017 (Daniele Procida) | widely adopted (Canonical, many) | *organising whole doc sets* into four types |
| **GitHub community-health files** | ~2018 | platform-enforced (Community Standards) | file names + locations (root/`.github`/`docs`) |
| **llms.txt** | Sept 2024 (Jeremy Howard) | *proposed convention*, contested | an AI-facing index file at site root |
| **AGENTS.md** | Aug 2025 | open format, Linux Foundation / Agentic AI Foundation; *no required fields* | agent-facing instructions |
| **purposeful-readme** | 2026 | experimental spec (`v0.1.0`) | AI-native README: fixed order + machine-metadata block + anti-drift |

**Old but still essential**: README, CONTRIBUTING, LICENSE, SemVer, Keep a
Changelog, Contributor Covenant — all 2011–2017, all still load-bearing. Age is
not obsolescence here; these are stable precisely because they are simple.
**Genuinely new / in flux**: llms.txt (2024, *contested* — Google's John Mueller
and others note no major LLM provider commits to reading it; treat as optional,
not a bet), AGENTS.md (2025, fast-spreading, 60k+ repos), purposeful-readme
(2026, experimental — interesting direction, not yet safe to mandate).

### How the standards treat AI readers (the modern shift)

Three distinct philosophies, worth separating because they answer different
questions:

1. **AGENTS.md — a second file for the agent.** README stays human; agent
   instructions move to a predictable sibling. No required schema (it is "just
   Markdown"); the agent reads the *nearest* file in a monorepo tree. keur
   already follows this.
2. **purposeful-readme — make the *one* README machine-parseable.** Fixed
   section names + mandatory order + an embedded machine-metadata block, so a
   validator or agent parses it "without heuristics or guesswork", and an
   explicit anti-drift stance ("guarding against the drift and overreach that
   make READMEs go stale"). This is the same drift thesis as this ADR, arrived
   at independently — strong corroboration.
3. **llms.txt — a curated index, not the content.** An H1 name, a blockquote
   summary, then `## sections` of annotated links to the *real* Markdown. It is
   a map, not a destination — the same "link don't copy" principle at site scale.

The convergent lesson across all three: **AI reads structure, not prose.** Stable
headings, predictable location, and links-over-duplication are exactly what makes
docs parseable by an agent *and* scannable by a human. This ADR's "chapters +
cross-link + deterministic guard" is the repo-level expression of that lesson.

### Diátaxis — the framework for everything beyond the README

Diátaxis is the most load-bearing find for *scope*. It says all docs serve one of
four needs, and mixing them in one page is the root cause of bad docs:

| Type | Reader need | keur home |
|------|-------------|-----------|
| **Tutorial** | learning by doing (newcomer) | README quick-start / a `docs/` getting-started |
| **How-to guide** | a specific task (knows the domain) | `docs/` + README "Local setup" steps |
| **Reference** | look up facts (while working) | `CONTRIBUTING.md`, `config/keur.toml`, `--help` |
| **Explanation** | understand *why* | `docs/adr/`, `docs/design/vision.md` |

keur's existing split already *is* Diátaxis without naming it: ADRs = explanation,
CONTRIBUTING = reference, README = tutorial-ish quick start, `docs/design` =
explanation. Naming it gives the "where does this paragraph belong?" question a
principled answer and prevents the four root files from swelling — content that
is really a how-to or reference belongs in `docs/`, linked, not inlined. It also
maps cleanly to the audience tree: a *consumer* wants tutorial+reference, a
*contributor* wants how-to+explanation.

### Essential vs. optional, by maturity (the scope answer)

Not every repo needs every file. Gate by the project profile (ADR-011/026) so a
prototype is not nagged for governance it does not need:

| Maturity | Essential | Add as it matures |
|----------|-----------|-------------------|
| **prototype** | README (Context + Local setup) | — |
| **mvp / internal** | + LICENSE, full README (6 sections), AGENTS.md | CHANGELOG |
| **public / library** | + CONTRIBUTING, SECURITY, CHANGELOG (Keep-a-Changelog), SemVer | CODE_OF_CONDUCT, standard-readme order |
| **mission-critical** | all of the above, enforced | issue/PR templates, CODEOWNERS |

This is the single most important scoping decision: **the standard is a sliding
scale, not a checklist**. A one-weekend prototype with a README is *correct*; a
900-repo org (the APS RFC's context) retrofitting active repos to the six-section
README is *also* correct. The repo-type (`product`|`library`|`both`) and maturity
together select the bar — never a flat "every repo needs all 11 files".

### CMMI doc-ladder — bare minimum per level, and keur's control at each

keur already uses **CMMI 1–5 as its `maturity` axis** (ADR-011) — a *requested
quality bar for this codebase*, borrowing CMMI's ladder (honest scope note from
ADR-011: it describes org-process maturity, not product safety; here it is a
per-repo bar). So "what docs at CMMI level N?" is not a new model — it is making
the doc-column of that existing ladder explicit. The philosophy per rung, with
the concrete `KEUR-DOC-*` control that enforces it:

| CMMI | Name | Doc bare-minimum (cumulative) | Philosophy — *why this rung* | keur control (verdict) |
|------|------|-------------------------------|------------------------------|------------------------|
| **1** | Initial | `README` with **Context** + **Local setup** only | Ad-hoc is *allowed*. The one thing a chaotic repo still owes a reader: what is this, how do I run it. Nothing else is required — nagging a prototype drives people to `--no-verify`. | DOC-005 checks only those two headings exist · **warn** |
| **2** | Managed | + `LICENSE`, full 6-section `README`, `AGENTS.md` | Work is now *tracked*; others (incl. agents) touch it. Legal clarity (LICENSE) and a complete README become the floor; AGENTS.md so an agent has one owner for commands. | DOC-001 presence (README/LICENSE/AGENTS) · DOC-005 all 6 sections · **warn** |
| **3** | Defined | + `CONTRIBUTING`, `SECURITY`, `CHANGELOG` (Keep-a-Changelog), SemVer; **cross-links required** | Process is *standardised & documented*. Contribution rules, a disclosure contract, and a versioned change history exist and are consistent. This is where "one owner per fact" stops being advice and becomes a checked invariant. | DOC-001 full set · DOC-002 cross-links present · DOC-003 command-subset consistent · **warn**, escalating |
| **4** | Quantitatively Managed | + `CODE_OF_CONDUCT`, doc-coverage *measured & gated* | Process is *measured*. Drift is no longer a smell you may ignore — the doc-coverage number is a threshold. Governance (CoC) is explicit. | DOC-002/003/005 flip **warn → block** under `--strict`; `eval/doc-coverage.sh` gated (ADR-010) |
| **5** | Optimizing | + issue/PR templates, CODEOWNERS, drift-regression fixtures | Process is *self-improving*. Every past doc-drift becomes a regression fixture so it cannot recur; routing (CODEOWNERS) and structured intake are enforced. | DOC-* all block · a `@proves` fixture per historical drift (defect-prevention, ADR-055) |

Three principles make this a ladder, not a checklist:

1. **Cumulative, never resetting** — level N includes everything below it. You
   climb; you do not swap.
2. **warn before block** — a control is introduced as `warn` one or more levels
   *before* it blocks (DOC-002/003/005 warn at 2–3, block at 4). The repo is
   *briefed* on the bar before it is *gated* on it (ADR-037 — emit the rule up
   front, do not ambush). This is the anti-`--no-verify` stance: a human or agent
   always sees the clear fix first.
3. **Required-set is `max()` across axes** — CMMI sets the *process* floor, but
   `lifecycle × criticality` (ADR-011) can raise *which content docs* are
   required independently. A `cmmi-2` but `tier-1` service still needs an `ops`
   runbook because criticality demands it. keur resolves the strictest.

**How keur helps, in one line:** the doc bar stops being a wiki page nobody
re-reads and becomes a **pre-commit control that scales with the repo** — the
same binary nudges a prototype gently and gates a mission-critical service hard,
with a self-explaining fix every time (ADR-035). The gate *is* the standard;
there is no second system to keep in sync.

### How overlap is prevented (synthesis of the research)

Every modern standard converges on the same anti-overlap mechanism, which this
ADR adopts:

- **One owner per fact** (this ADR §1) — the fact lives once; others link.
- **README is an entry point and a map, not a destination** (purposeful-readme,
  llms.txt) — long content moves to companion files (`docs/`, `INSTALL.md`).
- **Separate by reader-need** (Diátaxis) — a how-to and a reference never share
  a heading, so they cannot drift into each other.
- **Companion files, added only when a section outgrows the README**
  (purposeful-readme's "conditional minimalism") — not pre-created empty shells
  (Diátaxis explicitly warns against empty four-folder scaffolds).

## The broader standards landscape — GitLab's list ↔ GitHub's locations

The GitLab project sidebar nudges a repo to "Add LICENSE / CHANGELOG /
CONTRIBUTING / Wiki / CI/CD / Kubernetes / Integrations". GitHub rewards the same
files but surfaces them differently: via the **Community Standards** checklist
and by auto-detecting **community-health files** in three locations — the repo
**root**, a **`.github/`** folder, or a **`docs/`** folder (verified against
GitHub docs). An org can also set org-wide defaults in a special
**`<org>/.github`** repository. Mapping, so neither platform's prompt is a
mystery:

| File / feature | GitLab prompt | GitHub location & surface |
|----------------|---------------|---------------------------|
| `README.md` | sidebar "README" | root/`.github`/`docs`; rendered on repo home |
| `LICENSE` | "Add LICENSE" | root; licence badge (keur: MIT ✓) |
| `CHANGELOG.md` | "Add CHANGELOG" | root; conventional, not platform-mandated (keur ✓) |
| `CONTRIBUTING.md` | "Add CONTRIBUTING" | root/`.github`/`docs`; linked on new issue/PR (keur ✓) |
| `CODE_OF_CONDUCT.md` | (community) | root/`.github`/`docs`; Community Standards checklist |
| `SECURITY.md` | (security policy) | root/`.github`/`docs`; "Report a vulnerability" flow (keur ✓) |
| `CODEOWNERS` | code owners | `.github/`, root, or `docs/` (GitLab: `.gitlab/`) |
| Issue/PR templates | `.gitlab/issue_templates/`, `merge_request_templates/` | `.github/ISSUE_TEMPLATE/`, `PULL_REQUEST_TEMPLATE.md` |
| Funding | — | `.github/FUNDING.yml` (sponsor button) |
| CI/CD config | "CI/CD configuration" (`.gitlab-ci.yml`) | `.github/workflows/*.yml` (keur ✓) |
| `AGENTS.md` | — | root (nested per-package in monorepos); not platform-surfaced yet |

Treat the whole set with the same "one file, one job — link don't copy"
discipline. The decision above governs the four instruction files; `KEUR-DOC-001`'s
existence check can later extend to the community-health set, gated on keur's
maturity profile (ADR-011/ADR-026) so a prototype is not nagged for a
`CODE_OF_CONDUCT.md` it does not need. Because GitHub accepts root/`.github`/`docs`,
the presence check must search all three, not just the root.

## Consequences

- **Positive**: humans and agents can never be given conflicting build commands,
  because the facts live in one owner and the guard fails loudly when a copy
  appears. Drift becomes a finding, not a surprise.
- **Positive**: keur eats its own dog food — it guards its root docs with its own
  engine, the same credibility story as `KEUR-ARCH-*` self-checks.
- **Positive**: the shim-by-reference rule means adding a new agent tool is one
  symlink, not another file to maintain.
- **Negative / cost**: three new self-check rules + fixtures + a `@proves` e2e
  that injects a duplicated command and asserts `KEUR-DOC-003` fires (guard
  against a dead rule, as `test_selfcheck.sh` does for `KEUR-ARCH-001`).
- **Risk (named, bounded)**: the command-token subset check (DOC-003) can
  false-positive on a command that legitimately appears only in an agent file
  (rare). Mitigate with the standard `keur:ignore KEUR-DOC-003` marker (ADR-035)
  and keep it `warn`.

## Follow-ups

1. Author `rules/keur-internal/KEUR-DOC-001..005.rule` (presence incl. the
   root/`.github`/`docs` three-location search, cross-link absence,
   cross-reference command-subset, README-section presence by repo-type).
   Self-explaining messages per ADR-035.
2. Add `[project] type` (`app` | `service` | `library` | `util` | `monorepo`)
   to the config (ADR-011), **declared not guessed**, defaulting to `util` when
   unset; wire it to DOC-005c's required-chapter set. keur declares
   `type = "util"`.
3. Fixtures: a drifted-doc fixture (agent file names `make verify`, contributing
   does not) → DOC-003 fires; a README missing "Release" on a `product` →
   DOC-005 fires; a clean root → zero findings.
4. `tests/e2e/test_selfcheck.sh`: assert zero `KEUR-DOC-*` findings on this repo,
   then inject a temporary duplicated command and assert DOC-003 fires.
5. Trim any already-duplicated build facts out of `AGENTS.md`/`README.md` down to
   a link into `CONTRIBUTING.md`, so this repo passes its own new guard; align
   each doc to its chapter set (section 1b).
6. Add the "repeat links, not facts" line, the ownership table, and the chapter
   sets to `AGENTS.md`'s "After a change" / `CONTRIBUTING.md`'s doc section so
   authors know the convention before the rule catches them.
7. Document the Diátaxis mapping (tutorial/how-to/reference/explanation →
   README/`docs/`/CONTRIBUTING/ADRs) in `docs/` so "where does this paragraph
   belong?" has a principled, standards-backed answer and the four root files
   stay thin.
