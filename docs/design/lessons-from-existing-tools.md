# Lessons from existing static-analysis tools

Status: reference notes (informs ADR-004)
Date: 2026-09-17

Before growing KEUR's engine (ADR-004), we studied how the established
open-source tools solved the same problems — to steal the wins and avoid the
known dead ends. Sources: the Semgrep team's own history
("Semgrep: a static analysis journey", r2c 2021), and the AppSec community's
open-source SAST comparisons (Semgrep, SonarQube CE, CodeQL, Bandit, Brakeman,
gosec, Bearer, PHPStan, Flawfinder; plus Gitleaks/TruffleHog for secrets).

## The single most important lesson (from Semgrep's author)

Semgrep grew out of Coccinelle/Spatch (Linux kernel API evolution) → Facebook's
Sgrep (PHP best-practice enforcement) → Semgrep. When Facebook evaluated the
heavyweight tools (Coverity, Fortify, Semmle/CodeQL) their verdict was:

> "They were too slow, reported errors too late in the development cycle,
> addressed only very general classes of bugs, and had a high false positive
> rate. This was not because they were badly engineered, but because of the
> difficulty of the problem... Detecting null pointer dereference requires
> complex interprocedural context-sensitive and control-flow sensitive
> analysis... **No amount of engineering will solve the halting problem.**
> In contrast, Sgrep was fast, catching bugs even before they were committed,
> and high signal... usually few to no false positives."

**Takeaway for KEUR:** do NOT chase "find every possible vulnerability". Aim at
*best-practice violations and known-bad patterns* — high signal, low false
positive, fast, pre-commit. That is exactly KEUR's positioning (deterministic
guardrails), and it is a feature, not a limitation. This retro-justifies
ADR-001 (line engine) and bounds ADR-004 (grow deliberately, never toward a
full interprocedural prover).

## Wins worth stealing

1. **"A pattern that looks like source code."** Semgrep's rules read like the
   code they match, with two additions: **metavariables** (`$X`, `$A` — abstract
   away specific names) and **ellipsis** (`...` — ignore irrelevant statements
   between two anchors). This is what enables cross-line matching (`kmalloc(...)`
   then later `memset(...)`) WITHOUT forcing the rule author to think about an
   AST. If KEUR adds a structural/DSL rule type (ADR-004 stages 2/5), copy this
   ergonomics: patterns that look like code + metavars + ellipsis beat inventing
   a new query syntax.

2. **Generic AST across languages.** Semgrep wrote one matcher against a generic
   AST (inspired by GitHub's Semantic), exploiting the structural similarity of
   most languages; improvements to the matcher benefit every language. This is
   precisely ADR-004's "language-neutral rules + per-language adapters". Do NOT
   build one matcher per language.

3. **Boolean combinators in rules.** `patterns: [pattern-not-inside, pattern-either,
   pattern-not]`. Maps directly onto the small expression DSL ADR-004 stage 5
   proposes (`all/any/not/inside`). Steal the shape.

4. **Semantic equivalences, added incrementally.** Semgrep normalises things like
   keyword-argument order, constant propagation, alias resolution — added over
   time, not up front. Lesson: ship the syntactic matcher first, add semantic
   normalisation only where it pays.

5. **Language-specific tools have the lowest false-positive rates in their niche.**
   Brakeman knows `User.find(params[:id])` is parameterised by ActiveRecord and
   does not flag it; generic tools do flag it (false positive). Bandit/gosec
   likewise. Lesson: KEUR's language adapters should carry framework/idiom
   knowledge, and a generic rule should be overridable by a language-specific
   one — which is exactly what the `.keur/rules` cascade (project > global >
   built-in) already allows.

6. **Multi-tool stack + dedup.** The recommended open-source setup is Semgrep
   (broad) + a language-native tool + a secrets scanner, with findings merged
   and **deduplicated** ("a SQL injection flagged by both should be one finding").
   KEUR already treats other proven tools as ground-truth sources
   (groundtruth-awssec, groundtruth-cpm) and dedups at (repo,file,category).
   Keep that; add SARIF output so KEUR interoperates with the same pipelines.

## Traps to avoid (what the tools consistently get wrong / cannot do)

- **Interprocedural cross-file taint is where open-source stops.** Input enters
  in controller.x, flows through service.x, hits a sink in dao.x — free tools
  miss it, commercial ones charge for it, and it is genuinely hard. KEUR should
  NOT pretend to do this in early stages; scope the source→sink model (ADR-004
  stage 4) to within-file/within-fragment first.
- **"Any sanitizer = safe" is a real false-negative source.** Tools assume a
  sanitizer on the path neutralises the flow, missing sanitizer bypasses. If
  KEUR models sanitizers, record it as an assumption, not a proof.
- **Second-order (stored-then-used) flows** defeat static analysis regardless of
  price. Out of scope; do not chase.
- **Community rule quality varies wildly** — Semgrep's own registry has
  high-FP community rules. This is the SAME problem as KEUR's learned-rule slop:
  a rule that passes locally can be noise elsewhere. KEUR's answer (benchmark
  gate + holdout + slop/perturbation test + composite-candidates) is the right
  instinct; keep the gate strict.
- **CodeQL's power came at the cost of a steep query language.** Its QL has a
  real learning curve; Semgrep won adoption by being easy. Lesson for KEUR's
  future DSL: optimise for "a developer with no program-analysis background can
  write a rule" (Facebook's Sgrep succeeded precisely because non-experts wrote
  rules).

## Concrete follow-ups for KEUR (tracked, not yet done)

- SARIF output format (interop with GitHub/GitLab code scanning, the lingua
  franca of the whole ecosystem).
- Metavariable + ellipsis ergonomics if/when a structural rule type lands.
- Keep leaning on the multi-tool ground-truth approach (cpm, aws-security-agent,
  and candidates: bandit/gosec/brakeman/gitleaks) rather than reimplementing
  their detections from scratch.
- Framework-aware idioms in language adapters to keep the low-FP promise.
