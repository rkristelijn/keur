# AGENTS.md — working agreements for keur (humans and AI agents)

The short version. Full reasoning: `CONTRIBUTING.md` and `docs/adr/`.
Big picture: `docs/design/vision.md`.

## What keur is

A single compiled C++ binary that runs as a git pre-commit/pre-push gate:
staged files → RE2 rule engine → findings → deterministic policy → PASS/WARN/BLOCK.
Simple, proven tricks over complexity. 80% fit is enough. Be honest about limits.

## The way forward (how to write code here)

- **Small, single-purpose functions.** Name it precisely → test it in isolation.
- **Right dispatch (ADR-014):** list grows + data branches → lookup table; list
  grows + real behaviour/extension point → strategy registry; short & closed →
  plain `if/else` is fine. Not "no if/else".
- **Name conditions** instead of cryptic inline logic: `if (isEligible)`, not
  `if (a >= 18 && !b && c)`. Testable without the flow.
- **Positive booleans** (`isValid`, never `isNotInvalid`); prefix `is/has/can/should`.
- **CQS:** a function is a query (returns value, no side effects) or a command
  (side effect) — never both. Data verbs: `get/fetch/find`.
- **I/O at the edge (ADR-003):** side effects in `adapters/` only; decision logic
  stays pure.
- **Files small:** well under ~300 net lines; split by responsibility. The limit
  is the complexity check.
- **Deviate for performance only, and say so** in a comment.

## Non-negotiables

- **English only.** All code, identifiers, comments, commit messages, ADRs and
  docs are written in English — no exceptions, regardless of the language the
  request came in.
- **Follow the existing structure; don't invent a parallel one.** Read these
  before adding anything: `CONTRIBUTING.md` (layout + build + conventions),
  `docs/design/architecture.md` (the layers: `cmd/` orchestrators, `adapters/`
  I/O edge, `scan/` dispatch, `rules/` data, `config/` resolution, `common/`
  platform seam), and `docs/adr/` (the why). New code goes in the layer that
  already owns that responsibility.
- **Test-first for engine/core changes.** Characterise with tests, refactor,
  tests stay green, then extend. Run `cd src/keur && make test` — must be green.
- **Traceability (ADR-010):** annotate with `@id` (declare a concept),
  `@implements <id>` (code that builds it), `@proves <id>` (test that proves it).
  Check with `eval/doc-coverage.sh`. Refactor-proof: link to IDs, never file:line.
- **keur guards itself:** `KEUR-ARCH-*`/`KEUR-PORT-*`/`KEUR-QUAL-*` run on this
  repo. Don't bypass with `--no-verify`; fix what the clear error tells you.
- **One binary, no runtime plugins.** Engines are compile-time strategy objects
  in `rule/engines.cpp` (ADR-014), not loadable modules (ADR-002).
- **Rules are data; the engine is dumb (ADR-009).** New detection = a `.rule`,
  not engine logic.

## Handling review findings (quick win vs ADR)

When a review (CodeRabbit, SonarCloud, a human) raises a finding, triage it by
effort before touching code — this keeps fixes and design decisions separate:

- **Quick win** → fix it now **and** add the control that prevents its return in
  the same change (fix + control, never just the fix). A pattern-detectable
  anti-pattern becomes a `.rule`; a logic bug gets a `@proves` test.
- **Not a quick win** (heavy lift, cross-cutting, or a semantic/design change) →
  write an **ADR** capturing the decision, and for the MR at hand apply only the
  minimal fix the reviewer asked for. Don't smuggle a design change into a
  bugfix PR.

Rule of thumb: if you can state the fix *and* its control in one sentence and it
touches one layer, it's a quick win. If it changes a definition, an interface,
or spans languages/layers, it's an ADR.

## After a change

1. `cd src/rules && make && cd ../keur && make test` — build both, tests green.
2. `eval/doc-coverage.sh` — no new dead links or untraced concepts.
3. If you added an engine: one entry in `rule/engines.cpp`, a `@proves` test,
   and document it in `docs/rules-authoring.md`.
