# What keur does that other tools don't

keur is not a linter, not a test runner, not husky. It sits in the gap
**between git and your native tools**, and it judges on **patterns and repo
structure, not deep language semantics**. That position is the whole point:
it catches a class of problems no single-language tool is even looking at.

```text
   git (commit / push moment)
        │
        ▼
   ┌──────────────────────────────────────────┐
   │  keur  — the gate in between              │
   │  pattern + structure + git-state checks   │
   │  one tool, every language, every file     │
   └──────────────────────────────────────────┘
        │
        ▼
   native tools (ESLint, flake8, clang, jest, …)
   deep single-language semantics
```

## The dividing line: pattern/structure vs. language semantics

A linter parses one language's AST and enforces that language's rules. keur
deliberately does **not** need to understand the language. It matches patterns
(RE2), checks for the presence/absence of things, and reads repo + git
structure. That is weaker than a compiler on any single language — and far
broader, because it works the same on a Dockerfile, a README, a shell script,
a YAML file and a .py, in one pass, at the one moment that matters: the commit.

Consequence: keur is the right tool for cross-cutting, cross-language,
process-level problems. It is the **wrong** tool for "is this TypeScript type
correct" — that's tsc's job, and keur happily defers to it. ADR-066 makes external
scanners opt-in; the wired adapters are gitleaks, semgrep and shellcheck. To gate on
tsc, add a project-local check.

## vs. the tools people already have

| Tool | What it owns | What it canNOT see (but keur can) |
|------|--------------|-----------------------------------|
| **husky / pre-commit framework** | *runs* hooks; a plumbing layer. You still have to write every check yourself, per repo. | keur IS the checks: a shipped, versioned ruleset + a verdict model (PASS/WARN/BLOCK) + per-rule explainers, installed once. husky runs your bash; keur gives you the bash's worth of logic for free, consistently, across repos. |
| **ESLint / flake8 / clang-tidy** | deep semantics of ONE language, one file at a time. | Dockerfiles, YAML, markdown, shell, git artefacts, repo structure. Cross-file/repo rules. AI-slop. They never look outside their language. |
| **jest / pytest (test runners)** | run the tests you wrote. | whether the test actually *asserts* anything. keur flags size-only (`assert len(x)==3`) and weak (`toBeTruthy`) assertions — a green suite that verifies nothing. The runner can't tell a real test from a fake one. |
| **gitleaks / trufflehog** | secrets, by entropy/patterns. | everything non-secret: PII (email/phone/IP), root Dockerfiles, conflict markers, house rules. And keur's secret layer coexists (ADR-035 value-based suppression). |
| **semgrep** | closest peer: multi-language patterns. | keur bundles a maturity/profile verdict model (prototype vs mission-critical held to different bars, ADR-011), a git-hook install, per-rule `keur learn` explainers, and runs on its own RE2 engine free/offline — no rules-as-a-service. |

## The three things only a between-git-and-tools gate can catch

### 1. Git / process state (not code at all)

- Committed **merge-conflict markers** (`<<<<<<<` / `>>>>>>>`) — block,
  critical (KEUR-QUAL-007). The file is broken; no language linter sees a git
  artefact.
- Candidate / custom: **commit straight to main**, missing branch protection,
  commit without a linked issue — process rules that live at the git moment,
  not in the code. keur's pre-commit/pre-push tiers are the natural home.

### 2. Repo structure & multi-language (cross-file)

- Dockerfile runs as **root** (KEUR-IAC-002 / DK-SEC-010) — container escape
  risk your JS linter never looks at.
- Missing README / LICENSE / SECURITY / keur config at the required maturity.
- PII literals (email/phone/IP, SEC-013) across any file type.
- A **custom** house rule in ~10 lines (e.g. English-only docs, DEMO-NL-001) —
  plugs into the same gate + explainer as the built-ins. ESLint can't express
  "our docs must be English".

### 3. AI-slop — the modern differentiator

When you give an AI the free hand, these recur, and no linter flags them
because they are syntactically valid:

- **Fake tests**: size-only / weak assertions (SLOP-100/101/102/107) — looks
  like a test, asserts nothing, CI is green, you're not covered.
- **console.log as a test** (SLOP-105).
- **AI filler phrases / promotional headers** (SLOP-001/119, QUAL-013) —
  "As an AI…", marketing-style file headers instead of a one-line purpose.  <!-- keur:ignore QUAL-013 SLOP-001 — deliberate quote of the anti-pattern as an example, not actual slop -->
- **TODO without an issue** (QUAL-077) — AI strews TODOs that are never tracked.
- **NIH reimplementation** (SLOP-114) — rewrites what the stdlib already has.
- **`unsigned >= 0`** (SLOP-117) — an always-true comparison, a logic tell.

## One-line positioning

> ESLint knows your language. jest runs your tests. husky runs your hooks.
> keur is the gate between git and all of them — it catches the cross-language,
> structural, process and AI-slop problems none of them are looking at, on
> patterns, at commit time, in any repo.
