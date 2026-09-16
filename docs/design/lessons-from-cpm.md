# Lessons from CPM — Design Input for KEUR

CPM was a prior experiment in building a fast, rule-driven code-quality and
security scanner that runs on git hooks. It proved that the *architecture*
scales to roughly a thousand checks and runs in under a second, but it never
proved that its *rules* were precise, and it never put its best engine on the
default execution path. KEUR inherits CPM's skeleton and deliberately invests in
the boring, decisive work CPM skipped: a benchmark corpus, measured precision,
enforced contracts, and a distribution story that matches its promises.

This document distills what to carry forward, what to avoid, the honest lessons,
the non-negotiables, and an ISO/IEC 25010 quality-attribute mapping for KEUR.

---

## 1. What CPM did WELL — carry into KEUR

- **Rules as declarative data, not scripts.** CPM's keystone decision. Rule
  definitions were data files, not code, giving a near-zero contribution
  barrier, self-describing rules, and bulk verifiability (a rules-smoke test).
  *Why:* it lets the ruleset grow without touching the engine, keeps rules
  reviewable by non-engine authors, and makes the whole corpus machine-checkable.

- **A small set of composable engine modes.** Modes like pattern / absence /
  presence / file-absence / file-presence / extract-duplicates / block-absence
  covered a wide surface with a tiny primitive set — including light structural
  analysis (resource-leak and duplicate-route style checks) without a full AST.
  *Why:* a small, orthogonal primitive set is easier to test, reason about, and
  keep deterministic than a sprawling per-rule codebase.

- **Linear-time, single-pass regex engine.** A RE2-style engine ran ~900
  untrusted patterns in a single linear-time pass, completing scans in under a
  second. *Why:* patterns are effectively untrusted input; a linear-time engine
  makes catastrophic backtracking (ReDoS) structurally impossible and keeps the
  hook fast.

- **A unified findings contract.** One JSONL schema for every engine, where every
  finding must carry a `fix`, plus first-seen commit tracking. *Why:* a single
  contract makes output consumable by CI/IDEs, and first-seen tracking enables
  new-only gating (block new violations without failing on legacy debt).

- **Git-hook integration with tiered latency budgets.** Separate budgets for
  pre-commit (fast), pre-push (medium), and CI (fuller) runs. *Why:* it matches
  the tool's thoroughness to the developer's tolerance at each stage, so the
  fast gate stays fast and stays enabled.

- **An inline, rule-specific, auditable escape hatch.** A `cpm:ignore`-style
  inline suppression, taught in the rule message itself. *Why:* suppressions
  become explicit, local, and reviewable in the diff rather than hidden in
  global config, keeping the gate honest without being tyrannical.

- **Dogfooding and review-mining as a rule-sourcing loop.** CPM ran on itself and
  mined real code-review comments to seed new rules. *Why:* it grounds the
  ruleset in real defects instead of speculation — a strong *sourcing* signal.

- **Process-per-check isolation (fork-join).** Each check ran as an isolated
  process with no shared state and no mutexes. *Why:* isolation eliminates a
  whole class of concurrency bugs and makes each check independently
  reproducible.

---

## 2. What CPM did LESS well — avoid / do differently in KEUR

- **Generation outran validation.** CPM's rules were essentially all machine-
  generated, and validation never went beyond CPM's own repository. *KEUR
  alternative:* treat generation and dogfooding as *sourcing only*; require every
  rule to pass an independent validation gate against a ground-truth corpus
  before it ships.

- **Unproven precision, no benchmark.** ~1000 rules with no ground-truth corpus
  and no measured false-positive / false-negative rate; at least one broad rule
  was known to over-fire. *KEUR alternative:* build the benchmark corpus *before*
  scaling rule count, and publish measured precision/recall — never a raw rule
  count as a quality badge.

- **The best engine was not on the default path.** CPM's flagship rule-scan
  engine was not wired into the default command, and a large fraction of default
  checks failed on missing external tools. *KEUR alternative:* the default
  command runs the flagship engine and delivers value with zero external tools;
  anything optional degrades gracefully, never fails the default run.

- **Heavy toolchain contradicted the "one binary" promise.** A C++20 build with an
  external regex dependency and ~14 external tools undercut the zero-dependency
  claim and forced awkward static-linking dilemmas. *KEUR alternative:* pick a
  language whose distribution genuinely matches the single-binary promise and
  which embeds a linear-time regex engine, so "one binary, zero deps" is true by
  construction.

- **An unresolved regex-engine tension.** Internal proposals pulled between
  dropping the linear-time engine (for a backtracking standard-library one) and
  keeping it — and dropping it would have reintroduced ReDoS across untrusted
  patterns. *KEUR alternative:* make linear-time regex a fixed, non-negotiable
  constraint from day one so the question never reopens.

- **A mid-migration, half-implemented framework.** Two generations of framework
  coexisted, the findings contract was only partially implemented, and logic was
  duplicated across languages. *KEUR alternative:* one framework, one contract,
  enforced in CI from the first commit; no parallel half-migrations.

- **High test count, shallow assertions, no negative tests.** Many tests but weak
  assertions, and no tests proving a rule *doesn't* fire on clean code. *KEUR
  alternative:* mandate both a positive fixture (rule fires on a real violation)
  and a negative fixture (rule stays silent on clean code) for every rule.

- **A monolithic engine file whose own findings weren't burned down.** The core
  scan implementation grew into a ~1000-line monolith, and the tool didn't fix
  the issues it flagged in itself. *KEUR alternative:* keep the engine modular
  and hold KEUR to its own gate — zero self-findings on the default profile.

---

## 3. Lessons from the CPM experiment

CPM's most important lesson is that **architecture and validation are two
different projects, and CPM only finished the first one.** The design was right:
rules as data, engine as code; a small composable primitive set; a linear-time
scanner; a unified findings contract; tiered git hooks; auditable inline
suppressions. That skeleton scaled to ~1000 checks and stayed fast. None of that
needs to be re-invented.

What CPM demonstrated by omission is that **a rule you cannot measure is a rule
you cannot trust.** Shipping ~1000 rules with no benchmark corpus meant nobody
could say whether the tool was accurate or merely loud. A raw rule count looks
impressive and proves nothing; the number that matters is precision on known
inputs, and CPM never produced it. The temptation to scale count before scaling
confidence is the single most expensive mistake to avoid.

The second lesson is that **your default command is your product.** CPM's best
engine existed but was not what a user got by running the default command, and
many default checks broke on missing tools. A tool is judged by its out-of-the-
box behaviour, not by a capability buried behind flags. If the flagship engine
isn't the default, it effectively doesn't exist for most users.

The third lesson is about **honesty in constraints.** CPM promised one binary
with zero dependencies but was built on a stack that made that promise hard to
keep, which surfaced later as painful static-linking and engine-swap debates.
Choosing a language and toolchain whose natural distribution matches the promise
removes an entire category of future pain. And a security constraint like
"linear-time regex only" must be fixed early, because reopening it invites the
reintroduction of the exact vulnerability (ReDoS) the constraint was meant to
prevent.

Finally, CPM showed that **generation and validation must be kept separate.**
AI-assisted generation and review-mining are excellent *sources* of candidate
rules. They are not evidence that a rule is correct. Conflating the two is how a
corpus becomes large and unreliable at the same time.

---

## 4. Must-haves for KEUR (non-negotiables)

1. **Rules are data; the engine is code.** Adding a rule never requires touching
   the engine. The whole rule corpus is machine-verifiable.
2. **Linear-time regex, always.** The matching engine is linear-time by
   construction. This constraint is permanent and not subject to later trade-off.
3. **A ground-truth benchmark corpus exists before the rule count scales.**
   Precision and recall are measured and published; rule count is never used as a
   quality metric.
4. **Every rule ships with a positive and a negative fixture.** One proving it
   fires on a real violation, one proving it stays silent on clean code.
5. **The default command runs the flagship engine with zero external tools.** Out
   of the box, KEUR delivers its core value. Optional integrations degrade
   gracefully and never fail the default run.
6. **One binary, dependencies matching the promise.** The distribution story is
   true by construction — a single self-contained binary that runs identically on
   every developer machine and in CI.
7. **A single findings contract, enforced in CI from commit #1.** One schema,
   every finding carries a `fix`, first-seen tracking enables new-only gating.
8. **Determinism.** The same input produces the same findings on every run, on
   every machine. A non-deterministic gate is worthless.
9. **Tiered latency budgets with an auditable inline escape hatch.** Fast pre-
   commit, fuller CI; suppressions are inline, rule-specific, and visible in the
   diff.
10. **KEUR passes its own gate.** Zero self-findings on the default profile; the
    engine stays modular, never a monolith.

---

## 5. ISO/IEC 25010 quality attributes mapped to KEUR

The attributes most critical for a deterministic security gate on git hooks are
**Reliability** (determinism), **Performance Efficiency** (pre-commit speed),
**Security** (the tool must itself be trustworthy), and **Maintainability**
(rules as data). These are marked **high** and drive the design.

| Ility | What it means for KEUR | Priority | How KEUR should address it |
| --- | --- | --- | --- |
| **Reliability** | Deterministic results and no crashes — the same code yields the same findings on every run and machine; malformed input never brings down the scan. | **High** | Pure, side-effect-free rule evaluation; linear-time engine (no backtracking blowups); process-per-check isolation; fault-tolerant file handling; property tests asserting run-to-run identical output. |
| **Performance Efficiency** | Sub-second pre-commit scans; low CPU/memory so it runs on every commit on a laptop; scales to large repos. | **High** | Single-pass linear-time matching; tiered latency budgets (fast pre-commit / fuller CI); scan only staged/changed files on pre-commit; benchmark the hot path and hold a time budget in CI. |
| **Security** | The tool inspects sensitive source and must not leak it or be tampered with; rules and results must be genuine and auditable. | **High** | No network egress by default (nothing leaves the machine); signed/verifiable binary and ruleset; integrity-checked rules; auditable, in-diff suppressions; tamper-evident, attributable findings output. |
| **Maintainability** | Rules are the living heart of the tool; adding, tuning, and testing rules must be cheap and low-risk. | **High** | Rules as declarative data decoupled from a small composable engine; mandatory positive + negative fixtures per rule; bulk rule-smoke verification; modular engine (no monolith); clear analysability of why a finding fired. |
| **Functional Suitability** | If KEUR claims a rule, it must check it and flag real violations accurately, with correct location and severity. | **High** | Ground-truth benchmark corpus built before scaling; measured and published precision/recall; no silent gaps in the advertised scan surface; new-only gating to control legacy noise. |
| **Compatibility** | Outputs slot into existing pipelines and coexist with other hooks/linters. | Med | Standard findings output (SARIF/JSON) for CI, dashboards, and IDEs; stateless runs that don't lock shared resources; a single documented findings contract. |
| **Flexibility (Portability)** | Same binary and hook behave identically on macOS/Linux/WSL laptops and CI runners; scales from one repo to many. | Med | One self-contained binary with no external tool requirements; no platform-specific code paths; identical behaviour local and in CI; clean hook install/uninstall. |
| **Interaction Capability (Usability)** | Findings are immediately actionable and a pass/fail is obvious at a glance. | Med | Each finding shows file, line, rule, severity, and a fix hint; clear pass/fail summary; clear errors on misconfiguration (never a silent pass); readable, non-overwhelming output. |

---

*Reference: ISO/IEC 25010:2023 System and software quality models (the revision
that renamed Usability → Interaction Capability and Portability → Flexibility and
reorganized the sub-characteristics).*
