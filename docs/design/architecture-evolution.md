# keur — architecture evolution: from cpm to keur, and the road ahead

@id architecture-evolution

> The story behind keur: where it came from (cpm), which choices were made and
> why, how the architecture evolved, and — honestly — where there is still room.
> This document ties the separate ADRs and design docs into one narrative. Where
> a claim rests on a decision, the source is cited.

Sources: `../../../cpm/docs/research/cpm-lessons-learned.md`,
`../../../cpm/docs/architecture.md`, `lessons-from-cpm.md`, `vision.md`,
`keur-design-rationale.md`, `scope.md`, `../roadmap.md`, and ADR-001, ADR-002,
ADR-003, ADR-004, ADR-009, ADR-014, ADR-018, ADR-019, ADR-028.

Note on freshness: the ADRs record intent at their date; the running code has in
places moved ahead of them. Section 4 flags where an ADR's "not yet built" note
is now stale, verified against the current `src/keur/` tree.

---

## 0. The through-line, in one sentence

Both tools follow one idea: **rules are data, the engine is a dumb, fast,
linear-time executor, and its output is normalised evidence that a deterministic
policy turns into PASS/WARN/BLOCK.** cpm proved that architecture scales to
~1000 checks in sub-second time. keur inherits that skeleton and invests in the
boring, decisive work cpm skipped: a ground-truth corpus, measured precision,
enforced contracts, and a distribution story that matches the promise.

---

## 1. cpm — what it was, and what it proved

**cpm** (Code Project Maturity) is a single C++20 binary, ~87 source files, that
scans a repo for quality, security and maturity. It combines three check layers
(`cpm/docs/architecture.md`):

| Layer | Count | Technique |
|-------|------:|-----------|
| Rule engine | 909 `.rule` files (54 categories) | declarative data + RE2 |
| Shell checks | 188 scripts | Bash (Gen1/Gen2 framework) |
| Native C++ checks | ~30 | compiled into the binary |
| **Total** | **~1043** | |

cpm was explicitly a **100% AI-generated experiment** — zero hand-written code.
That matters for everything that follows: it explains both the speed at which
the corpus grew and its weak spot (§3).

### 1.1 The strong choices (which keur carries forward)

From `cpm/docs/research/cpm-lessons-learned.md`, point by point:

- **Rules as declarative data, not scripts.** ~850–909 of cpm's checks are plain
  `.rule` text files, parsed by one engine. A rule is data: `id`, `severity`,
  `category`, `target`, `patterns`, `message`, `fix`. The contribution barrier is
  near-zero, every rule is reviewable in isolation, the metadata is uniform, and
  the whole corpus is bulk-verifiable. **This is the keystone.**
- **A small set of composable engine modes.** `pattern`, `absence`, `presence`,
  `file-absence`, `file-presence`, `extract-duplicates`, `block-absence`. The
  sweet spot: `block-absence` and `extract-duplicates` capture a surprising
  amount of "light structural analysis" (resource leaks, duplicate routes)
  *without* a full parser.
- **RE2 single-pass scanning.** Linear time, no catastrophic backtracking —
  critical when running ~900 user-authored (effectively untrusted) patterns.
  Scan < 1s. The same choice ripgrep makes.
- **A unified findings contract.** Every check — C++, shell or rule — emits the
  same `Finding` record (JSONL). Every finding must carry a `fix`; `first_seen`
  commit tracking enables "block only new findings".
- **Git-hook integration with tiered latency budgets.** pre-commit (<5s) /
  pre-push (<60s) / CI (~2min). The tiering is worth more than any single check —
  it is what makes shift-left *adopted* rather than disabled.
- **`cpm:ignore <rule-id>` inline escape hatch**, taught in the rule message
  itself. Auditable and rule-specific; stops people from disabling the whole
  linter.
- **Self-dogfooding + review-mining.** cpm runs on itself and mines real
  code-review comments (`gitlab-review-audit.sh`, `coderabbit-harvest.sh`) into
  rules. A strong *sourcing* loop.
- **Fork-join process isolation.** `fork()/waitpid()` instead of threads: each
  check in its own process, no shared state, no mutexes.

### 1.2 The weak spots (which keur deliberately does differently)

Also from `cpm-lessons-learned.md` — the honest retrospective:

- **Generation outran validation.** "100% AI-driven... rules are not yet
  validated on real-world projects beyond this repo." Correctness is asserted by
  construction, not measured.
- **~1000 rules of unproven precision, no ground-truth benchmark.** No labelled
  corpus, no measured false-positive/false-negative rate. A "1149 checks" badge
  signals quantity, nothing about precision. `SEC-038`
  (`\b(md5|MD5|sha1|SHA1)\s*\(`) demonstrably over-fired.
- **The most powerful engine was not on the default path.** `rule-scan` (the
  ~900 rules) was a **separate binary, not integrated into `cpm check`** — while
  `cpm check` failed on 11/63 checks because `cpm install` did not install the
  tools they need. The default experience under-delivered while the best
  capability sat behind a second binary.
- **C++/build complexity vs. the "zero dependency" promise.** The engine needs
  RE2 (external C++ dependency); a full `cpm check` needs ~14 external tools;
  building needs a C++20 toolchain. The "one binary" promise and the RE2 reality
  were in tension (ADR-164 even proposed replacing RE2 with `std::regex` — which
  would reintroduce ReDoS).
- **Two-generation shell framework mid-migration.** Gen1/Gen2 side by side; the
  "unified findings contract" was aspirational (partially implemented);
  duplicated logic across C++ and shell.
- **Shallow tests despite a high test count.** "11956 passed", but size-only
  asserts (`CHECK(findings.size() == 3)`) without checking *what* was found; no
  negative tests.
- **Monoliths.** `scan_checks.cpp` as a 1092-line monolith — exactly the kind of
  debt cpm's own complexity rules target.

### 1.3 cpm's one-line summary

> cpm proved the *architecture* is sound (data-driven rules + one linear engine +
> unified findings + tiered git hooks) and scales fast to ~1000 checks. It did
> **not** prove the *rules* are precise, because generation outran validation and
> the flagship engine never reached the default command.

---

## 2. keur — the same skeleton, different priorities

keur is a clean-room re-implementation of cpm's engine design (ADR-001), with the
same cpm-compatible `.rule` format. Not a fork, not a dependency on the cpm
binary — its own engine in `namespace keur`. The positioning (`vision.md`):

```text
staged files → rule engine → findings → deterministic policy → PASS / WARN / BLOCK
```

The non-negotiable core (`lessons-from-cpm.md` §4): rules are data; linear-time
regex, always; a ground-truth corpus before scaling the rule count; every rule
with a positive *and* a negative fixture; the default command runs the flagship
engine with zero external tools; one binary; one findings contract enforced from
commit #1; determinism; tiered budgets with an auditable inline escape hatch; and
keur passes its own gate.

### 2.1 The foundational choices (ADRs)

| ADR | Decision | Why |
|-----|----------|-----|
| **ADR-001** | Native RE2 engine, own implementation | A shell prototype (process-per-rule) was correct but took minutes; RE2 is linear, no ReDoS; the `.rule` format stays cpm-compatible |
| **ADR-002** | One C++ binary, no runtime plugins | Distribution: one artifact, no plugin runtime |
| **ADR-003** | Provider/adapter layer; I/O at the edge | Decision logic stays pure (testable); platform/parser swappable |
| **ADR-005** | Platform abstraction (posix/win32) | macOS/Linux/Windows with no `#ifdef` in business code; `KEUR-PORT-*` guards drift |
| **ADR-009** | Composite rules & facts — *engine stays dumb* | Conditional, framework/version-aware findings as **data**, not as engine brain (~1113 lines, 0 hardcoded rule IDs) |
| **ADR-010** | Traceability `@id`/`@implements`/`@proves` | Refactor-proof, grep-fast, links decision→code→test |
| **ADR-011** | Project profile (owner/lifecycle/criticality/maturity) | A prototype and a mission-critical service run the same hook, held to different bars |
| **ADR-014** | Engine registry: engines are compile-time strategy objects | Extension without a plugin runtime; the policy stays the sole referee |
| **ADR-019** | External-tool scanners + project-local adapter | keur *orchestrates* proven tools (it is not a linter re-implementation) |

### 2.2 What keur adds on top of cpm

- **More engines.** Beyond cpm's set: `count`, `multiline`, `max-lines`,
  `max-nesting`, `max-dir-files`, `path-derived`, `composite` (ADR-028 verifies:
  no cpm rule uses an engine keur cannot run).
- **Facts + `requires:` (ADR-009).** A cheap context pass derives per-target
  `langs/frameworks/versions/runtimes` from *direct* manifest pins; a rule can
  declare `requires:` and is otherwise never loaded. "Most-discriminating-first"
  as data: a JS repo never pays for Java/Docker rules. Composites combine
  existing rules with `all/any/not` — the engine gains an evaluator, not a brain.
- **The learn loop (ADR-023, `learning-loop.md`).** The distinguishing piece. A
  local LLM (Ollama) *proposes* the smallest `.rule` that would catch a missed
  case; a deterministic **acceptance gate** measures against ground truth and
  accepts only if recall rises, precision stays above the floor, and the regex
  generalises (does not memorise — `overfit-regex` is rejected). **The benchmark
  decides, never the LLM. Your IP never leaves your machine.**
- **Ground truth kept separate from the detector** (`keur-design-rationale.md`).
  The immutable evaluation slice is never shown to the optimiser → no overfitting.

### 2.3 The analysis-engine evolution (ADR-004) — deliberately staged

keur today is a line-oriented RE2 scanner. ADR-004 records a *direction*, in
stages, never a big-bang rewrite. Each stage is independently shippable and must
earn its keep on the corpus before the next begins:

```text
Stage 0 (done)  RE2 line engine (ADR-001) + provider-arch + learn loop
Stage 1         tokenizer + skip_comments/skip_strings  → kills comment/string FPs
Stage 2         block/scope/extract engines             → cross-line without a parser
Stage 3         light structural index (not a full AST)
Stage 4         source/sink/sanitizer flow
Stage 5         expression DSL (all/any/not/inside/...)  ← ADR-009 composites are this, additively
Stage 6         language adapters + inline code (shell-in-yaml, sql-in-cpp, ...)
```

The strategic bound is explicit: keur is **not "another Semgrep".** Semgrep/
CodeQL win on detection depth (30+ parsers, interprocedural taint) — racing them
there is a losing game, and even commercial tools hit the halting-problem wall.
What keur uniquely has is the **learn layer**: it distills the *union* of what the
field detects into one fast, deterministic RE2 set. "More than Semgrep" in
**breadth, speed and self-improvement — never in interprocedural depth."

---

## 3. The turning point: ADR-028 — sunset cpm, keur is the successor

Originally (ADR-018, 2026-09-20, *parked*) the idea was to run cpm as a second,
independent quality gate over keur's own code. That was additive but not urgent,
and had an unsolved problem: cpm scanned the gitignored `eval/corpus/repos/`
anyway (1375+ fixture findings drowned the signal).

**ADR-028 (2026-09-25) decides otherwise and supersedes ADR-018:**

1. **cpm is sunset; keur is the successor** for both the hook and the pipeline.
   keur gates itself.
2. **No bulk import of cpm's ~1004 rules.** They are largely unvalidated /
   machine-generated — exactly the failure mode keur was built to avoid.
   Candidates come in **through the eval loop**: a cpm rule is adopted only when
   it raises precision/recall on the ground-truth corpus. Adopted rules get
   `source: cpm` (provenance) + `maturity:` (ADR-026); framework-specific rules
   sit behind a `requires: framework:` guard.
3. **keur must run in the pipeline, not only as a hook.** This is the one
   capability keur still has to grow to fully replace cpm (§4).

The resulting mental model:

```text
keur   → HOOK MANAGER + pipeline gate.  commit / push / CI:  "may this pass?"
keur-eval → the yardstick (immutable corpus):                "is keur getting better?"
```

cpm's ~24 internal `[checks]` do not all become keur rules; they fall into four
buckets (ADR-028): most are **proven-tool adapters** (gitleaks, semgrep,
shellcheck, clang-tidy, cppcheck, yamllint, markdownlint, clang-format) — so
scanner-dispatch (ADR-019), not rule or engine work. Rule of thumb:
pattern-matchable → a `.rule`; dataflow/AST/cross-file → orchestrate the proven
tool; a per-file scalar → a small file-level engine; a number you report but do
not fail on → a metric, not a gate.

---

## 4. Where there is still room (the honest list)

This is where corners were cut or, more positively, where the biggest leverage
still lies. Based on the `Coverage:` fields in the ADRs, the roadmap statuses,
and — importantly — a re-check against the current `src/keur/` code, because the
implementation has in places overtaken the ADRs.

### 4.1 Already done — the ADRs/roadmap that describe these as "stub" are stale

Two prominent "not yet built" claims are now **built**, verified in the code:

- **Scanner-dispatch is wired into `check`.** ADR-019's implementation note
  (2026-09-20) said `cmd/check.cpp` dispatched *only* the RE2 engine and that the
  dispatch loop "does not exist today". It does now: `check.cpp` includes
  `scan/dispatch.h` and `config/scanners.h`, resolves the scanner inventory from
  `keur.toml`, applies a per-tier filter (`config::tier_scanners`), runs the
  proven-tool adapters + keur-rules + project-local over the real adapter seams
  (`adapters::Proc`), and calls `scan::dispatch_scanners(ctx)`.
- **The policy engine is a real engine, not a grep-for-"block" stub.** Roadmap
  T-204 described the current policy as "the current grep-for-'block' stub".
  In `check.cpp` the verdict is now a pure function of the aggregated findings:
  `finding_blocks(action, confidence, block_at)` builds a `PolicyInput`, and
  `policy_evaluate(input)` returns PASS/WARN/BLOCK on one path (ADR-014, "no
  second, hidden code path can block"), self-explaining the blocking finding(s)
  on WARN/BLOCK.

So the "orchestrate proven tools + one policy engine" promise is materially in
place. Roadmap T-204 and ADR-019's implementation note should be updated to
reflect this.

### 4.2 The CI tier does not exist yet (still blocks the cpm replacement)

Verified: `config/keur.toml` declares `[tiers.ci]` (budget-ms = 0, the full
scanner set incl. trufflehog/grype/osv-scanner/tfsec/presidio), but
`check.cpp` only parses two tiers —
`o.tier = (args[i] == "pre-push") ? Tier::PrePush : Tier::PreCommit;` — so
`--tier ci` silently falls back to pre-commit. ADR-028 follow-up #1 still stands:
add `Tier::Ci` (enum + parse), whole-tree scope, the full scanner set, non-zero
exit on BLOCK, and a machine-readable report (JSON/SARIF). Without it the
"passed locally, failed in CI" gap (the miss that motivated ADR-019) does not
close, and keur cannot take over cpm's pipeline role. Related: a `--gate` mode
for `keur scan` (which today always exits 0 — a report, not a gate).

### 4.3 The dependency-management brick is missing

- **T-104 `keur doctor`** (🔴) — verified absent: no `doctor` command in
  `main.cpp` or `cmd/`. It should detect which of the 9 proven tools are present,
  report gaps, and give exit-code health. The roadmap puts it first
  ("makes the tool honest about what it can do").
- **T-201** `keur doctor --install <tool>` — guided install via the platform
  package manager, never a silent auto-install.

### 4.4 Corroboration and calibration are not wired yet

- **T-203** — evidence aggregator + **corroboration** (same fingerprint from 2+
  tools in a group → confidence bonus). Verified absent from `scan/dispatch.cpp`.
  The dispatch runs the scanners and collects findings, but does not yet reward
  agreement between them.
- **T-306** — confidence calibration learned from the corpus (so "0.93" ≈ 93%
  correct) is still roadmap.

### 4.5 The learn loop (M3) and rule absorption (M4) are large tracks

- **M3 (train)** — T-301..306: generalise the self-improving evaluator into
  `keur eval`, corpus splits with an immutable evaluation set, acceptance gate +
  Pareto report, iteration history with two-layer memory. The scripts run
  (`eval/optimise.sh`, `eval/history/`), but promoting this to a first-class
  `keur` subcommand with the full safety controls is still ahead.
- **M4 (absorb)** — T-401..406: the eval-gated import of cpm candidates. Engine
  coverage is there (pattern+presence+absence ≈ 99% of directives), but the
  import *path* (measure → promote, `source: cpm` tagging) is still to build.

### 4.6 Small, concrete gaps

- **`exclude_names:` parser key** (ADR-028 follow-up #2) — verified absent from
  `src/rules/`. 3 cpm rules use it; keur silently ignores it today. A small
  data-only extension + a `@proves` test makes those 3 rules load 1:1.
- **Suppression infra** (T-207/208/209) — inline `keur:ignore <RULE-ID>` exists
  in the engine (ADR-001), but repo-level baseline (`.keur/allowlist`,
  accept-existing-debt / block-only-new) and feeding FP suppressions back into
  the corpus (`expect_not`) are still 🔴.
- **`comment-ratio`** (ADR-028) — the one cpm check that genuinely warrants a
  small new file-level engine (RE2-free, modelled on `max-lines`). `complexity`
  is deliberately *not* put in the RE2 engine (the ADR-001 invariant): either
  approximate via `max-nesting`, or delegate to cppcheck/lizard as an adapter.
- **Output formats** (T-205) — JSON + JUnit partly exist; SARIF (for
  CI/dashboards) is later work.

---

## 5. What you take away from this

1. **The architecture is proven; the precision must be earned.** cpm's skeleton
   is sound and inherited; keur's entire reason to exist is the measured,
   eval-gated path — accept that it is slower than a bulk copy. That is the point.
2. **The default command *is* the product.** cpm's biggest lesson. keur's default
   runs the RE2 engine with zero external tools, orchestrates proven tools when
   present, and degrades gracefully when not — and that dispatch + policy path is
   now actually built (§4.1), not a stub.
3. **The engine stays dumb, forever** (ADR-009). Every temptation to put
   framework/CVE/version logic *inside* the engine is rejected — it is data in
   `rules/` and the folder structure. That is what makes "build once, let it
   scale" true.
4. **The biggest untapped leverage is the CI tier + doctor + corroboration**
   (§4.2, §4.3, §4.4): that is what carries keur from "a good hook" to "the full
   cpm successor", and it is exactly the work ADR-028 marks as follow-up.

---

## 6. Engineering feedback — strengths and opportunities

> This section is **feedback, not a claim**. It is one reviewer's read of the
> project as engineering, written to be argued with. Nothing here asserts that
> keur *is* trustworthy or *does* replace review — it maps where the confidence
> is earned and where it is still faith. The distinction matters because the
> project's own thesis is "a rule you cannot measure is a rule you cannot trust,"
> and that standard should be applied to keur itself.

### 6.1 Strengths (what is genuinely good engineering)

- **Rules as data, engine as a dumb executor.** The thing that grows (the corpus)
  is cheap to review in isolation; the thing that is hard to get right (the
  engine) is small and stable. ADR-009 explicitly refuses to put a brain in the
  engine — holding that line under pressure is the mark of the discipline.
- **Linear-time regex made a fixed constraint.** A security property (no ReDoS on
  untrusted patterns) was turned into a non-negotiable rather than a
  packaging-convenience trade (cpm nearly swapped RE2 for `std::regex`; keur
  will not). Making the important constraint permanent is good engineering.
- **Honest, written self-critique.** `lessons-from-cpm.md` diagnoses the prior
  tool's failure ("generation outran validation") plainly. That document is the
  reason keur is a better project than cpm, not merely a rewrite.
- **Determinism and a single policy path.** "AI may add evidence; AI may never be
  the referee" is the correct shape for a gate, and it is enforced in code (one
  `policy_evaluate` path, no hidden second way to block — §4.1).
- **Asking "can I trust this?" instead of assuming it.** The question itself is
  the peer-review instinct. A project that writes down its own open gaps (§4) is
  already doing part of the review it worries it is missing.

### 6.2 Opportunities (where confidence is not yet earned)

- **The ground-truth corpus is the whole game, and it is the weakest link — by
  design.** Every precision number is ceilinged by label quality. Until there is
  a stable precision/recall on a *held-out* slice that was never trained against,
  "keur passed" means "keur was consistent," not "keur was correct." Consistency
  is not correctness. Closing this is what M3 exists for.
- **A solo, fully-vibe-coded tool validated by tools the same author vibe-coded
  has a correlated-failure risk.** If the author's model of "what is dangerous"
  is wrong, it is wrong in the code, in the rules, and in the corpus — one blind
  spot in three places. The defence is deliberately importing *independent*
  provenance: Sonar, CodeRabbit, gitleaks, semgrep. The ADR-004 "distill the
  union of the field" thesis is, read this way, the strongest idea in the design
  precisely because it breaks that correlation.
- **Mining Sonar/CodeRabbit into rules is *sourcing*, not *validation*.** A rule a
  tool suggested is still unvalidated until measured on ground truth the author
  did not author — restating cpm's exact mistake if skipped.
- **A pre-commit gate replaces a narrow class of review.** Per `scope.md`, keur
  stops *dangerous* and *slop* (secrets, injection, insecure IaC, hallucinated
  APIs). Even a perfectly trusted keur replaces the *mechanical* review layer; it
  does not replace design review ("is this the right abstraction, does this hold,
  was the problem understood") — that is out of scope by keur's own definition.
- **Keep the independent tools in the loop permanently, not only as rule-sources.**
  Running them *beside* keur and treating the disagreements as the signal is the
  cheapest way to keep the author's blind spots visible — but note the sharp
  limit in §6.4: those tools have their own false positives, so they are a
  *disagreement source*, never a clean label source. The options for turning
  independent provenance into a standing control are in **ADR-031**.

### 6.4 The corpus-trust problem (the hardest one)

The reference tools (Sonar, CodeRabbit, semgrep, gitleaks) **do produce false
positives** — that is not an edge case, it is their normal behaviour. So a ground
truth built by trusting their output inherits their false positives as "labels."
Training or measuring against that corpus optimises keur toward *reproducing other
tools' mistakes*, and calls it precision. This is the deepest version of the
correlated-failure risk: it is not that the author's labels might be wrong, it is
that the *automatically-sourced* labels are *known* to be partly wrong.

The consequence is uncomfortable but clear: **a trustworthy ground truth cannot be
"whatever the tools flagged." It has to be adjudicated** — each candidate label
confirmed as a true positive or demoted to a known false positive before it counts
as truth. The question of *how* to adjudicate at corpus scale (and whether a local
LLM can do the first pass) is a design decision in its own right — see **ADR-032**.

### 6.5 The one question this raises: re-run the whole corpus through the gate?

Yes — periodically, and it is itself an architectural decision (see **ADR-030**).
The corpus, the engine, and the ground truth all drift: rules are added, the
ground truth is rebuilt, the overfit heuristic changes. A rule that passed the
gate six weeks ago is not guaranteed to still pass against today's ground truth.
Without a scheduled full-corpus re-measure, "every rule is gated" silently
degrades into "every rule was gated *once*, against a corpus that no longer
exists." The options — cadence, what triggers a re-run, and what a regression
does — are laid out in ADR-030.

---

*This document is descriptive, not a decision. The decisions live in the ADRs it
references; if an ADR and this document ever conflict, the ADR wins.*
