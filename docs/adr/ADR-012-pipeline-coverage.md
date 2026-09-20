# ADR-012 — Pipeline coverage (which CI/CD stages a project must have)

@id ADR-012
**Status**: proposed · **Date**: 2026-09-19 · **Coverage**: code · **Relates to**: ADR-010 (traceability — same "measure presence from explicit signals" idea), ADR-011 (project profile — required stages follow from lifecycle × criticality × maturity)

## Context

keur gates the *code* at commit time. But a project's safety also depends on
*what the pipeline does*: does it lint, test, scan for secrets, run SAST/DAST/SCA,
produce an SBOM, sign artifacts? A repo can pass the commit gate and still ship
from a pipeline that never scans dependencies or signs its images.

We want **pipeline coverage**: measure which CI/CD stages a repo actually has,
against the set its profile *requires*. Same philosophy as ADR-010's traceability
coverage — measure presence from explicit signals (here: jobs/steps in the CI
config), fast, language-agnostic, and report the gap.

Which stages are *required* is not fixed: it follows from the ADR-011 profile.
A pilot needs lint+test+build; a Tier-1 production-ready service needs the full
security and supply-chain set. Shift-left applies: catch "no SCA on a live
service" here, not in an audit.

## Decision

Define a standard, grounded stage taxonomy (below), measure presence by reading
the CI config, and require a subset per profile. Grounded in recognised
frameworks so the list is defensible, not ad-hoc:

- **OWASP DevSecOps Guideline / DSOMM** — the security stages and their
  shift-left ordering (secret+SAST at commit, SCA+container at build, IaC before
  deploy, DAST against staging).
- **SLSA** — supply-chain stages (SBOM, build provenance, signing/attestation).
- **NIST SSDF** — the overarching secure-development framework.
- **Test pyramid** — the test layers (unit → integration → e2e).

### Stage taxonomy

**Group A — correctness / quality** (per commit, cheapest feedback)

| Stage | Source | Typical moment |
|-------|--------|----------------|
| `format` | convention | commit |
| `lint` | convention | commit |
| `type-check` | convention | commit |
| `unit-test` | test pyramid | commit |
| `coverage-threshold` | test pyramid | commit/PR |
| `mutation-test` | test quality | PR (optional) |
| `integration-test` / `e2e-test` | test pyramid | PR/merge |

**Group B — security** (OWASP DSOMM, shift-left)

| Stage | Source | Moment |
|-------|--------|--------|
| `secret-scan` | DSOMM | commit |
| `sast` | DSOMM | commit/PR |
| `sca` (dependency vulns) | DSOMM | build |
| `iac-scan` (misconfig) | DSOMM | before deploy |
| `container-scan` | DSOMM | build |
| `dast` | DSOMM | against staging |
| `iast` | DSOMM | staging (optional) |

**Group C — supply chain** (SLSA)

| Stage | Source | Moment |
|-------|--------|--------|
| `sbom` | SLSA | build |
| `provenance` (build attestation) | SLSA | build |
| `sign` (artifact/image signing) | SLSA / cosign | build/release |
| `license-check` | convention | build |

**Group D — release / operational**

| Stage | Source | Moment |
|-------|--------|--------|
| `smoke-test` | convention | post-deploy |
| `health-gate` / `rollback` | ITIL / SRE | deploy |

### Required set follows the profile (ADR-011)

Illustrative — the exact matrix is tuned with the blocking matrix ADR:

| Profile | Required (block if missing) |
|---------|-----------------------------|
| `pilot` | `lint`, `unit-test`, `build` |
| `mvp` | + `secret-scan`, `sast` |
| `business-ready` | + `sca`, `coverage-threshold`, `integration-test` |
| `production-ready` **or** `tier-1/2` | + `iac-scan`, `container-scan`, `dast`, `sbom`, `sign` |
| `maturity: cmmi-4+` | + `mutation-test`, `provenance`, no missing stages tolerated |

### How presence is measured

keur reads the CI config (`.gitlab-ci.yml`, `.github/workflows/*.yml`, etc.) and
maps a stage to **evidence of a known tool or job**: a step invoking `semgrep`
→ `sast` present; `trivy`/`grype` → `container-scan`/`sca`; `syft` → `sbom`;
`cosign` → `sign`; `gitleaks`/`trufflehog` → `secret-scan`. Grep-based, tool-
aware, language-agnostic — the same "presence from explicit signals" model as
the traceability meter. A stage with no recognised evidence is a gap.

Tool→stage mapping is data (config), so teams add their own tools without
touching the engine.

## Why not the alternatives

- **Require a fixed pipeline for everyone**: over-burdens pilots, under-serves
  Tier-1. Rejected for a profile-driven required set (ADR-011).
- **Trust a checkbox ("we have SAST")**: unverifiable. We read the CI config for
  actual evidence of the tool.
- **Run the scanners inside keur**: keur is a fast pre-commit gate, not a CI
  runner. It *measures that the pipeline runs them*, it does not replace them.

## Consequences

- Pipeline coverage is measurable and profile-aware: "Tier-1 live service, no
  `sign` stage → gap", caught early.
- The taxonomy is standards-grounded (DSOMM, SLSA, SSDF), so adopters and
  auditors recognise it.
- keur stays a gate, not a pipeline: it verifies the pipeline has the stages,
  the pipeline still runs them. Honest boundary, same as ADR-004's "we reproduce
  patterns, not graph analyses".
- The tool→stage mapping is data; new tools are configured, not coded.
