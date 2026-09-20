# ADR-011 — Project profile: owner, lifecycle, criticality, maturity

@id ADR-011
**Status**: proposed · **Date**: 2026-09-19 · **Coverage**: code · **Relates to**: ADR-009 (facts pass — the `technologies` list is auto-maintained there), ADR-010 (traceability — one of the controls a profile can require)

## Context

keur's gate should not be one-size-fits-all. A throwaway pilot and a
mission-critical payment service both run the same hook, but what should *block*
a commit differs enormously. Blocking hardening on a prototype makes people
disable the hook; not blocking it on a public service is negligence.

The strictness of the gate is a function of **who owns it, where it is in its
life, how much its failure hurts, and what quality bar the business has set** —
four independent things. This ADR defines those four axes precisely, grounds each
in an industry standard, and states what each level *means* and which controls it
activates. The axes live in `.config/.keur.toml` (and are read from an existing
`port.yml` when present, to avoid a second source of truth).

The axes are **orthogonal**: an MVP can be Tier-1 critical; a business can demand
a high maturity bar on an early-lifecycle product. No single axis is the master
knob.

> Parked (not in this ADR): a `technologies` list in the profile for fast
> "which versions run where" search, auto-maintained by keur's facts pass
> (ADR-009). Recorded here so it is not forgotten; specified later.

## Decision

Four axes define a **project profile**. Each is defined below with its standard,
its levels, what the level means, and the controls it activates. How the axes
*combine* into a verdict (the blocking matrix) is a separate decision (a later
ADR); this ADR fixes the vocabulary and the meaning.

### Axis 1 — `owner` (always a team, never a person)

**Definition.** The team accountable for the code. Always a *team*, never an
individual — individuals leave, teams persist, and accountability must survive
staff changes. A team identifier (e.g. a group handle or a distribution list).

**Standard.** Team-ownership is the norm in service-catalog / developer-portal
practice (Backstage, Port): every component has exactly one owning group.

**What it activates.** `owner` does not block anything. It routes: every finding
names the owning team ("owner: <team> — this is yours to fix"), so a human or an
AI agent knows who is accountable. Sole hard rule: it must be set and resolve to a
team, so no code is unowned.

### Axis 2 — `lifecycle` (where the software is in its life)

**Definition.** The release/product-maturity stage — who is exposed to the
software and therefore how much is at stake. Distinct from the *process* SDLC
phases (plan/design/build/test/deploy); this axis is about the *product's*
readiness.

**Standard.** Product-lifecycle staging as used in developer portals (Port's
`lifecycle` property). Levels, from least to most exposed:

| Level | Meaning | Users exposed |
|-------|---------|---------------|
| `pilot` | Throwaway proof-of-concept, exploring feasibility | none / the author |
| `mup` (Minimum Usable Project) | Just usable internally, rough edges expected | a few internal users |
| `mvp` (Minimum Viable Product) | Smallest thing delivering real value | early / limited users |
| `business-ready` | Feature-complete for its purpose, wider rollout | broad internal / external |
| `production-ready` | Fully released, run as a live service | all real users |

**What it activates.** Rises with exposure. Secrets/injection block from the
lowest level (they hurt regardless of users). Hardening (security headers, TLS
config, non-root containers, pinned runtime versions) becomes blocking as the
software approaches real users — negligible at `pilot`, mandatory at
`business-ready`+.

### Axis 3 — `criticality` (how much its failure hurts)

**Definition.** The impact if the software is compromised, wrong, or unavailable
— independent of lifecycle. A pilot can still be Tier-1 if it touches money or
personal data.

**Standard.** Service/system criticality tiers, as used by GitLab, Atlassian and
Microsoft's cloud-adoption framework. Four tiers, most to least severe:

| Tier | Meaning | Failure impact |
|------|---------|----------------|
| `tier-1` | Mission-critical | Direct revenue / safety / legal impact; outage felt company-wide |
| `tier-2` | Business-critical | Significant business disruption, but not existential |
| `tier-3` | Business-operational | Noticeable impact if down for days; workarounds exist |
| `tier-4` | Administrative / non-critical | Minor inconvenience; little downstream impact |

**What it activates.** Raises the floor regardless of lifecycle. A `tier-1`
project blocks on data-handling, authn/authz, secrets, and injection even at
`pilot`, because a mission-critical breach is unacceptable at any stage. Also
drives *data-class* controls (PII/secret handling stricter at tier-1/2).

### Axis 4 — `maturity` (the quality bar the business demands)

**Definition.** The *process discipline* level the organization requires for this
code — a deliberate, business-set bar, not a measured fact. This is the knob that
lets a business say "even for our MVP we require a Defined process."

**Standard.** CMMI (Capability Maturity Model Integration), the five-level model
(ISO/IEC — Wikipedia/IEEE). **Honest scope note:** CMMI describes an
*organization's process maturity*, not a product's safety. We borrow its ladder
as a **requested quality bar** for *this* codebase, not as a claim about the org.

| Level | CMMI name | Meaning as a keur bar | Controls it requires |
|-------|-----------|-----------------------|----------------------|
| `cmmi-1` | Initial | Ad-hoc; no process guarantees | none beyond the lifecycle/criticality floors |
| `cmmi-2` | Managed | Basic discipline; work is tracked | secrets + injection block; findings must be owned |
| `cmmi-3` | Defined | Standardised, documented process | + hardening, + traceability coverage (ADR-010) required, + tests present for changed behaviour |
| `cmmi-4` | Quantitatively Managed | Measured, thresholds enforced | + coverage thresholds gated (`--strict`), + no warn-tier gaps allowed |
| `cmmi-5` | Optimizing | Continuous improvement, defect prevention | + regression rules required, + every new engine/decision proven and traced |

**What it activates.** A ladder of *process* controls layered on top of the
lifecycle/criticality *content* controls. At `cmmi-3`+ the ADR-010 traceability
coverage becomes a required control; at `cmmi-4`+ it is gated, not just reported.

### Axis 5 — `docs` (external documentation, per audience)

**Definition.** Links to the documentation that lives *outside* the repo, one per
audience. Documentation is not one blob — each audience needs a different
document, and industry frameworks name them distinctly.

**Standards.** Three recognised sources, each covering an audience:

- **Backstage TechDocs / arc42** — developer & architecture docs ("how it is
  built, how to change it"). Backstage's catalog `links`/`techdocs-ref` is the
  de-facto developer-portal norm; arc42 is the architecture-doc standard.
- **ITIL (SKMS — Service Knowledge Management System)** — distinguishes
  operational doc types by purpose: a **runbook** (action-oriented ops steps for
  incidents/deploys), a **knowledge-base / service description** (what the
  service is, its SLA, owner), and **user documentation** (how the end user uses
  it).

| Link | Audience | Standard | What it is |
|------|----------|----------|-----------|
| `dev` | developers | Backstage TechDocs / arc42 | how to build/change it; architecture |
| `ops` | operators / SRE | ITIL runbook | what to do on incident/deploy — the operational playbook |
| `user` | end users | ITIL user documentation | how to use the software |
| `service` | product/service owner, management | ITIL knowledge base | what it does, SLA, ownership, business context |

**What it activates.** *Which* docs are **required** follows from lifecycle ×
criticality — the same shift-left logic as the other axes:

- A `pilot` requires none (nothing to operate or hand over yet).
- Approaching real users (`business-ready`+) requires `dev` and `user`.
- A `production-ready` **or** `tier-1`/`tier-2` service **must** have an `ops`
  runbook — this is an ITIL operational-readiness requirement: no live-going of a
  critical service without a runbook. keur can enforce "critical + live but no
  `ops` link" as a blocking control, caught pre-commit rather than in a post-
  incident review.

## What "controls" means here

A **control** is a rule-category outcome the gate enforces. Categories keur
already reasons about: `secret`, `sqli`/`cmdi` (injection), `iac`/hardening,
`pii`, `transport`, plus the process controls (`traceability`, `tests`,
`regression`). Each axis-level above says which categories move from *warn* to
*block*. The exact category×level matrix — and how the four axes combine into one
verdict — is the subject of a follow-up ADR (the blocking matrix); this ADR
defines only the axes and their meaning.

## Configuration

```toml
# .config/.keur.toml
[project]
owner       = "platform-security"    # a TEAM, always
lifecycle   = "mvp"                   # pilot | mup | mvp | business-ready | production-ready
criticality = "tier-2"               # tier-1 | tier-2 | tier-3 | tier-4
maturity    = "cmmi-3"               # cmmi-1 .. cmmi-5  (requested quality bar)

[project.docs]                        # external docs per audience (which are
dev     = "https://…/techdocs"        #   required follows from lifecycle × criticality)
ops     = "https://…/runbook"         # ITIL runbook — required at production-ready / tier-1|2
user    = "https://…/user-guide"      # ITIL user documentation
service = "https://…/service-desc"    # ITIL knowledge base — what/SLA/owner
```

If a `port.yml` is present, keur reads `lifecycle` from it (single source of
truth) and only the remaining axes are set in `.keur.toml`.

## Principles (why this is fair, and honest)

These principles bound how the profile is used, and answer the fair question
"does this force everyone to use AI?".

- **Author-neutral.** keur gates *code*, never the *author*. It reads a diff and
  matches patterns; it has no idea, and must have no idea, whether a human or an
  AI wrote the code. A developer who writes safe, hardened, tested code by hand
  passes exactly as easily as an AI that does. That keur happens to catch
  AI-generated slop is a *consequence*, not a purpose. Framing the gate as
  "pro-AI" would break the neutrality that makes it trustworthy — and trust is
  what stops people bypassing it.

- **Required maturity is policy; measured maturity is fact; keur reports the
  gap.** The `maturity`/`criticality` you *set* is a business decision (a
  Tier-1 service *should* demand a high bar). What keur *measures* — coverage,
  hardening, traceability — is a fact about the code. The useful output is the
  **gap**: "this is Tier-1, requires CMMI-3, but measures CMMI-2 → blocked, here
  is what is missing." Shifting that gap left — onto the machine, before the
  commit — is the point.

- **Shift-left: prevention beats cure.** A finding that blocks locally in a
  second costs minutes; the same defect in production costs days plus trust.
  Local testing is orders of magnitude faster and cheaper than catching it in
  CI, review, or an incident. This holds for *every* author. That — not "use AI"
  — is the honest cost argument for a pre-commit gate.

## Why not the alternatives

- **One "environment" knob (dev/staging/prod)**: conflates lifecycle,
  criticality and quality-bar into one axis, so you cannot say "MVP but
  Tier-1" or "live but only CMMI-2". Rejected for four orthogonal axes.
- **CMMI as the sole driver**: CMMI measures the *organization*, not the
  software; using it alone would let a "CMMI-5 org" ship an unhardened public
  service. It is one axis (the requested bar), not the whole model.
- **Per-developer owner**: individuals leave; accountability must be a team.

## Consequences

- The gate's strictness is explicit, standards-grounded, and per-project — no
  hidden heuristics.
- The vocabulary is portable and industry-recognisable (Port lifecycle, service
  tiers, CMMI), so adopters and auditors already understand it.
- Ownership is always answerable (a team), which makes findings actionable for a
  human or an AI agent.
- The four axes are defined; the blocking matrix that combines them is the next
  decision, kept separate so the vocabulary is stable before the policy is tuned.
