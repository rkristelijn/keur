# ADR-013 — OWASP SAMM coverage & parked gaps

@id ADR-013
**Status**: proposed (parking lot) · **Date**: 2026-09-19 · **Coverage**: none · **Relates to**: ADR-010 (traceability), ADR-011 (project profile & docs), ADR-012 (pipeline coverage)

<!-- Coverage: none — this ADR is a deliberate parking lot of NOT-yet-built work;
     it maps scope against OWASP SAMM and records what is out of scope and why.
     Items graduate out of here into their own ADRs when picked up. -->

## Context

keur + ADR-010/011/012 cover real ground: traceability, a project profile
(owner/lifecycle/criticality/maturity/docs), and pipeline coverage. But "does
keur give us a full secure SDLC?" deserves an honest answer measured against a
recognised yardstick. **OWASP SAMM** (Software Assurance Maturity Model) is that
yardstick: 5 business functions, each with 3 security practices (15 total).

This ADR maps keur against all 15, states what is covered, what is out of scope
(and why), and **parks** the coverable-but-not-yet-built items for later. It is a
parking lot, not a commitment — items leave here into their own ADR when started.

Guiding distinction (from the ADR-004 / ADR-011 lines): keur can enforce
**presence and pattern** things (a scan runs, a doc link exists, a rule fires) —
it cannot enforce **human-judgement** things (is this threat model any good?).
That line decides what is parkable vs. permanently out of scope.

## SAMM coverage map

| Function | Practice | keur status | Note |
|----------|----------|-------------|------|
| **Governance** | Strategy & Metrics | out of scope | org-level program management, not a repo gate |
| | Policy & Compliance | **partial** | ADR-011 profile encodes policy (criticality→bar); compliance mapping parked |
| | Education & Guidance | **partial** | actionable FIX messages guide devs/AI; formal training out of scope |
| **Design** | Threat Assessment | **parked** | can enforce *presence* of a threat-model doc link, not its quality |
| | Security Requirements | **parked** | can enforce a security-requirements doc link per criticality |
| | Security Architecture | out of scope | human design activity; keur checks patterns, not architecture quality |
| **Implementation** | Secure Build | **covered** | ADR-012 pipeline coverage (build/sign/SBOM) |
| | Secure Deployment | **covered (partial)** | ADR-012 deploy-stage checks; secret-mgmt parked |
| | Defect Management | **parked** | findings exist; tracking/triage workflow + SLA-to-fix not built |
| **Verification** | Architecture Assessment | out of scope | human review; not pattern-enforceable |
| | Requirements-driven Testing | **parked** | link security requirements → tests that prove them (extends ADR-010 @proves) |
| | Security Testing | **covered** | SAST/DAST/SCA/secret via ADR-012; keur's own rule engine is the SAST layer |
| **Operations** | Incident Management | **partial** | ADR-011 requires an ops/runbook doc link for tier-1/live |
| | Environment Management | **parked** | IaC/config hardening rules exist; env-config drift checks not built |
| | Operational Management | out of scope | ongoing ops process, not a repo-time gate |

## Parked features (the backlog this ADR holds)

Coverable by keur's model (presence/pattern/doc-link), not yet built. Each is a
future ADR when picked up:

1. **Design docs as controls** — add `threat-model` and `security-requirements`
   links to `[project.docs]` (ADR-011); require them per criticality (tier-1/2
   must have a threat model). Enforces *presence*, not quality.
2. **Requirements-driven testing** — extend `@proves` (ADR-010) so a security
   requirement (a declared `@id`) must have a test proving it; report the gap.
3. **Defect management** — a finding lifecycle: acknowledged / suppressed-with-
   reason / must-fix-by, tied to criticality (tier-1 secret = fix now).
4. **Secret management (deploy)** — check that deploy config references a secret
   store, not inline secrets (beyond commit-time secret scanning).
5. **Environment/config drift** — detect divergence between declared env config
   and what IaC provisions.
6. **Compliance mapping** — map keur findings/controls to a compliance framework
   (ISO 27001 / SOC 2 control IDs) for audit evidence.

## Permanently out of scope (and why)

- **Security Architecture, Architecture Assessment, Threat-model *quality*** —
  these require human judgement about design. keur can check a threat model
  *exists*; it cannot judge whether it is correct. Enforcing presence is honest;
  claiming to assess quality would not be.
- **Org-level governance (Strategy & Metrics, Operational Management)** — program
  management across teams, not a repo-time concern. keur is a per-repo gate.

This is the same bright line as ADR-004: keur grows in breadth (more presence/
pattern controls) and honesty (explicit gaps), never into claiming human design
review.

## Consequences

- Scope is honest and measurable against a recognised standard (SAMM), so no one
  mistakes keur for a full assurance program.
- The parked list is a clear, standards-anchored backlog — each item has a known
  home (a future ADR) and a known shape (presence/pattern/doc-link).
- The out-of-scope list protects keur from over-promising (the same risk flagged
  for AI-neutrality and "more than Semgrep"): a tool trusted for what it honestly
  does is worth more than one that claims to do everything.
