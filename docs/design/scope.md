# KEUR — scope (in / out)

> Purpose in one line: **keep AI in check — stop rubbish from being committed.**
> KEUR is a deterministic *guard*, not a quality *coach*. That distinction decides
> what is in scope and what is not.

## The core question about cpm's rules

cpm has **1004 `.rule` files**, 184 check scripts, 23 global-hook checks. "Bring all
of cpm's knowledge and rules" is the right instinct — but with a crucial nuance:

- **cpm = Code Project Maturity.** Goal: make a project mature — quality, style,
  complexity, accessibility, docs, test coverage. Much of the 1004 is *style/quality*
  (132 a11y, 63 quality, 31 style, 36 test, 45 build).
- **KEUR = commit guard.** Goal: stop dangerous/wrong code (and AI slop) reaching the
  repo. A safety + hygiene gate, not a maturity coach.

If every cpm rule becomes a KEUR **blocking** check, you recreate the exact failure
mode you fight elsewhere: **noise that makes the tool unusable**, so people disable
it. A pre-commit that whines about a11y labels or comment-ratio does not stop AI
rubbish — it just slows people down and trains them to `--no-verify`.

## Resolution: bring ALL cpm rules, but as tiered EVIDENCE, not all as blockers

We take the whole cpm rule corpus — it is proven knowledge — but route it by
**intent**, not by "block everything":

| cpm rule class | KEUR role | Default action |
|----------------|-----------|----------------|
| secrets (80) | **gate** | BLOCK (corroborated by gitleaks/trufflehog) |
| security (38), backend/oauth/session/transport | **gate** | BLOCK/WARN by severity |
| supply-chain (72) | **gate** | WARN (corroborated by trivy/grype/osv) |
| iac / k8s / docker / ansible | **gate** | WARN (corroborated by checkov/tfsec) |
| slop / ai-ml "slop" patterns | **gate** | WARN — this is the "AI rubbish" core |
| quality / style / test / a11y / docs | **evidence** | ADVISORY (report, never block) |
| language idioms (go/rust/kotlin/…) | **evidence** | ADVISORY |

So: **all the knowledge is available**; only the *safety- and slop-relevant* subset
gates a commit by default. The rest runs as advisory evidence (and feeds the
evaluator + the AI-context filter). Any team can promote/demote per repo via config
(the `.config/scan.toml` blocking/disabled pattern from the consolidated-scan RFC).

## In scope (what KEUR IS)

1. **Commit/push guard** — pre-commit (fast) + pre-push (fuller), global + per-repo.
2. **Stop the dangerous stuff**: secrets, private keys, live credentials, PII leaks,
   dangerous shell (`rm -rf /`, `curl|sh`), known-vulnerable deps, insecure IaC.
3. **Stop AI slop**: the "slop" rule class + AI-ml anti-patterns (hallucinated APIs,
   placeholder/TODO dumps, promotional headers, made-up config).
4. **Deterministic decision**: findings → policy engine → PASS/WARN/BLOCK. Reproducible.
5. **Encapsulate proven tools** (gitleaks, trufflehog, trivy, grype, osv, semgrep,
   checkov, tfsec, the proven PII scanner). cpm's rules fill gaps those tools miss.
6. **Optional AI escalation** for the ambiguous band — as evidence, never as referee.
7. **AI-input filter**: shrink 10k LOC to the few regions worth sending to an LLM.
8. **A stable benchmark** (CPM-Eval bridge) proving KEUR gets better, not just different.

## Out of scope (what KEUR is NOT)

1. **Not a quality/maturity tool.** Format, lint, complexity, comment-ratio, test
   coverage, a11y compliance, doc completeness → that is **cpm's** job. KEUR may
   surface these as advisory evidence, but they never block a commit by default.
2. **Not a style enforcer.** Naming, import order, line length → advisory at most.
3. **Not the referee via AI.** The LLM proposes/classifies; the policy decides.
4. **Not a Sonar clone.** Study Sonar as a reference/evidence source; do not
   reimplement its quality engine (and post-2024-11 analyzers are SSALv1 anyway).
5. **Not a rule-authoring platform.** cpm owns rule authoring + the corpus. KEUR
   *consumes* rules; it adds only private/organisation-specific gap-filler rules that must stay off
   the public cpm.
6. **Not a CI orchestrator / dashboard.** KEUR gates commits/pushes and emits
   JUnit/SARIF; the pipeline/reporting lives in CI + the existing tooling.

## How cpm and KEUR relate (three responsibilities)

The cleanest mental model — **where each one runs**:

```text
cpm      → an ANALYZER. Runs in the pipeline / on-demand.   "how mature / how good is this?"
keur     → a HOOK MANAGER. Runs on commit / push.           "may this code pass?"
keur-eval / CPM-Eval → the yardstick (immutable corpus).    "is KEUR getting better?"
```

- **cpm belongs in the pipeline / analysis phase.** It is a maturity *analyzer*
  (quality, complexity, coverage, a11y, style). You run it in CI or ad-hoc to judge a
  repo. It produces a rich picture + a score. That work is allowed to take time — you
  do NOT want it on every commit.
- **KEUR is the hook-management layer.** Its whole job is to manage your git hooks —
  configurable, global and per-repo — and answer one question at commit/push time:
  *may this pass?* (PASS/WARN/BLOCK). Fast, deterministic, focused on "keep AI in
  check, no rubbish."

Nuance: KEUR is **not only pre-commit**. It manages *tiers* — a fast pre-commit
(secrets/PII/slop), a fuller pre-push (deps/SAST/IaC), and it can invoke the same
gate in CI. But its reason for existing is **managing the hooks**: the exact gap of
"configurable global + in-repo pre-commit/pre-push hook management".

Where they touch: KEUR **reuses** cpm's rule knowledge (the 1004 rules) as an
evidence source. KEUR is *not* cpm. cpm stays the analyzer/pipeline tool and the
place where rules are authored; KEUR is the gatekeeper on the hooks.

## One-line test

> Does it stop dangerous code or AI rubbish from being committed?
> **Yes → gate (block/warn). No but useful → advisory evidence. No → out of scope (cpm).**
