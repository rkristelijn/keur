# ADR-017 — Surfacing traceability, doc- and test-coverage metrics (parked)

@id ADR-017
**Status**: proposed (parked) · **Date**: 2026-09-20 · **Coverage**: none · **Relates to**: ADR-010 (traceability — the metric source), ADR-016 (doc coverage per maturity), ADR-012 (pipeline coverage)

<!-- Coverage: none — decision recorded, not yet built. Parked pending a choice
     between the two routes below and confirmation of what the SonarCloud plan
     supports. -->

## Context

keur produces high-signal quality metrics that today live only locally and in CI
logs, not on the dashboard where the rest of the quality picture sits:

- **Traceability / doc-coverage** (`eval/doc-coverage.sh`): @id uniqueness,
  doc→code (`@implements`), code→doc, engine→test (`@proves`), engine→doc,
  cmd→test. Sub-second, language-agnostic, already gate-able with `--strict`.
- **Test coverage**: the unit/integration/e2e suites (`make test`), plus mutation
  (`mull`) — currently pass/fail in CI, no coverage % surfaced.

The wish: show "logical test coverage" and "logical doc coverage" next to the
SonarCloud findings, so one dashboard tells the whole story.

The constraint: Sonar runs here via **SonarCloud automatic analysis** (the GitHub
App) — there is NO `sonar-project.properties` and NO scanner step in
`.github/workflows/ci.yml`. Autoscan does not accept arbitrary custom measures.

## Decision (to make)

Two routes, deliberately not yet chosen:

### Route A — own CI gate (small, fits "keur guards itself")

Keep SonarCloud autoscan as-is. Add a CI job that runs `doc-coverage.sh --strict`
and a coverage report, failing the PR on regression. The metrics become PR checks,
enforced, no Sonar reconfiguration, no new secret. Downside: two dashboards
(Sonar + the CI check), not one.

### Route B — Sonar external issues / measures (bigger, one dashboard)

Replace autoscan with a `sonar-scanner` CI step (needs `SONAR_TOKEN`), and feed
doc/coverage results in via `sonar.externalIssuesReportPaths` (generic issue JSON)
and/or a coverage report path. Everything shows in Sonar. Downside: changes the
analysis mode, adds a secret and CI surface, and must be verified against the
plan's feature set (external issues / custom measures availability).

## Why parked

- Route B changes the CI/Sonar architecture — that is a real decision, not a
  tweak on a green PR. It deserves this record and confirmation that the
  SonarCloud plan supports generic external issues before any wiring.
- Route A is cheap and aligns with the existing self-check philosophy (the metric
  is already gate-able with `--strict`), but gives up the single-dashboard goal.
- The metric source (ADR-010) is stable and already correct; surfacing it is
  additive and can wait without blocking anything.

## Consequences

- Until decided: the metrics stay enforceable locally and in CI logs
  (`doc-coverage.sh`), just not on the Sonar dashboard.
- When picked: Route A is a one-job CI change; Route B is a scanner migration +
  external-issue export from `doc-coverage.sh`.
