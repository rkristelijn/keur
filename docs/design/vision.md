# keur — the vision

@id vision

keur started as one thing: a git pre-commit hook that blocks bad code on
"whatever" pattern you give it. That core still holds and always will:

```text
staged files → rule engine → findings → deterministic policy → PASS / WARN / BLOCK
```

Everything below grows *on top of* that core without breaking it. This doc is the
one place the whole picture is told; each part links to the ADR that decides it.

## What keur is becoming

A **profile-driven, standards-grounded guardrail — your copilot for coding and
vibe-coding.** Not by adding complexity, but by keeping simple, proven tricks and
letting a small config say how strict to be.

- **A gate that fits the project.** A throwaway prototype and a mission-critical
  live service run the same hook but should not block on the same things. A
  project profile (`owner`, `lifecycle`, `criticality`, `maturity`, `docs`)
  decides what blocks vs. warns. → `docs/adr/ADR-011-project-profile.md`
- **A basis that grows with you.** A company installs a minimal global baseline
  (via `core.hooksPath`); it can grow as the business raises the maturity bar.
  A local repo overrides it downward — "this is only a prototype, just get the
  basics right." → ADR-011
- **Security & hardening by rules on known patterns.** Not a heavy taint engine —
  fast RE2 rules that catch secrets, injection, and missing hardening, with
  platform/plugin/library/runtime checks driven by autodetection.
  → `docs/adr/ADR-009-composite-rules-and-facts.md`
- **Contains AI-slop with simple tricks that work.** Not 100% — it does not need
  to be. What matters is that the errors are clear enough for a human *or an AI*
  to know exactly what to do next, so nobody reaches for `--no-verify`.
- **Traceable and measurable.** Decisions, code and tests link via `@id` /
  `@implements` / `@proves` — refactor-proof, grep-fast, and soon clickable in
  your IDE. → `docs/adr/ADR-010-traceability.md`
- **Pipeline-aware.** It measures which CI/CD stages a repo has (SAST, DAST, SCA,
  SBOM, signing…) against what its profile requires.
  → `docs/adr/ADR-012-pipeline-coverage.md`
- **Honest about scope.** Measured against OWASP SAMM: keur enforces the
  *presence/pattern* things and says plainly what needs human judgement.
  → `docs/adr/ADR-013-samm-gaps.md`
- **Manageable.** `keur manage` turns engines/rules/scanners on and off; `keur
  doctor` says what is configured, what is missing, and whether it works. *(ADR
  pending.)*

## Learns on your codebase — your IP stays yours

keur can **train its rule set on your own code** using established tools and a
**local LLM (Ollama)** — the learn loop measures against a benchmark and a
deterministic gate accepts only rules that generalise.
→ `docs/design/learning-loop.md`

Crucially, **your IP never leaves your machine.** The loop runs locally; it never
scrapes proprietary dashboards and never ships your code or findings to a third
party — a boundary the corpus rules already enforce.
→ `eval/extend/SOURCES.md`

So you get a detector tuned to *your* stack, for free, without handing your
source to anyone.

## Free — and a friendly way to pay it forward

keur is **MIT-licensed and free**. It *wraps* open-source tools (semgrep,
gitleaks, trivy, checkov, …) — those are theirs, and keur never claims otherwise.
But the engine, the wrapper, the manager and the learn loop are built and
maintained by a developer, and if keur saves you time, paying it forward is
welcome — never required, always appreciated:

- **A good rule** — open a merge request. This is the best thanks: a rule that
  helped you helps everyone. Contributing one must stay *simple*.
  → `CONTRIBUTING.md`
- **A donation** to the developer, or something off the **wishlist**.

Free is free. These are a friendly invitation, not a toll.

## The through-line

Every part above follows one idea: **measure the presence of explicit signals
(tags, tools, doc links) against a profile-driven requirement, fast and
deterministically — and be honest about the line between what a pattern can
enforce and what needs a human.** Simple solutions for complex problems; 80% fit
is enough. That is keur.
