# ADR-004 — Toward a language-agnostic analysis engine (staged evolution)

Status: proposed
Date: 2026-09-17

## Context

KEUR today (ADR-001) is a **line-oriented RE2 scanner**: read each file once,
run pre-compiled regexes with an extension/`content_contains` prefilter, emit
findings. That was the right call for a fast pre-commit gate and it still is —
it scans 1000+ rules in well under a second, deterministically.

But two forces push beyond pure line-regex:

1. **False positives the line model cannot avoid.** A rule for raw `system(`
   fired on the word "system(" inside a *comment* (the KEUR-ARCH-001 incident).
   A line scanner has no notion of comment vs code vs string. cpm already solved
   this with `skip_comments` / `skip_strings` (a tokenizer), plus block-scoped
   engines (`block-absence`, `extract`) that a single line cannot express.

2. **Real detections are cross-line and cross-language.** The valuable security
   findings (SQLi, command injection, secrets reaching a sink) are *flows*:
   untrusted input → optional sanitizer → dangerous sink. And that flow often
   crosses a language boundary — shell invoking python, C++ building a shell
   string, PHP issuing SQL, TypeScript writing innerHTML, YAML embedding a
   `run:` shell script that runs `python -c "..."`.

A design review proposed a fuller architecture to serve these: a pipeline of
composable passes (source → normalize → tokenize → structural index → rule
engine → findings), a small **language-neutral rule model** with
language-specific facts, a **source/sink/sanitizer** first-class concept, a tiny
**expression DSL** (`all/any/not/inside/argument/...`), and — for inline code —
a **LanguageFragment / ExecutionBoundary** model that nests one language inside
another instead of forcing one universal AST.

The pull is real. The risk is equally real: this is a different machine than
ADR-001 chose, and building it naively becomes a compiler project (templates,
macros, overload resolution, conditional compilation — the reasons ADR-001
deliberately avoided a full AST).

## Decision

Adopt the vision as a **direction**, reached in stages, never as a big-bang
rewrite. The RE2 line engine stays the fast default; richer capability is added
as **additional engine types and optional passes on top of it**, not as a
replacement. Each stage is independently shippable, tested, and gated by the
same benchmark discipline the rest of KEUR uses (findings are evidence; the
deterministic gate decides).

### Prior art in cpm — this is not greenfield

The sibling project `cpm` already walked most of this path and shipped it. KEUR's
line engine (ADR-001) derives from cpm's `adr-145` (pluggable rule engine).
The stages below map onto cpm ADRs that are already **implemented**, so each
stage is largely *adoption + adaptation into `namespace keur`*, not invention:

| KEUR stage | cpm prior art | what it gives |
|-----------|---------------|---------------|
| line engine (ADR-001) | cpm `adr-145` pluggable-rule-engine | single-pass RE2, per-ext index |
| Stage 1 tokenizer / skip_comments | cpm `adr-165` analysis-engine | comment/string stripping, 15+ langs |
| Stage 2 block/scope/extract | cpm `adr-166` rule-engine-extensions | block-absence, scope, extract, file-level |
| findings shape | cpm `adr-129` unified-findings-contract | one normalised finding record |
| slop as a signal | cpm `adr-167` ai-slop-detection | slop rules + how to treat weak signals |
| native C++ engine shape | cpm `adr-022` native-cpp-architecture | the performance-first layout |

The authoritative, current field/engine reference cpm maintains is
`docs/rules-authoring.md` in the cpm repo — when adopting an engine, that doc
(plus the implementation) is the source of truth, and KEUR mirrors the behaviour
into its own tests so the two stay compatible. cpm's `adr-165` also records the
same false-positive motivation we hit independently (KEUR-ARCH-001): its per-line
scanner's ad-hoc `//` / `/* */` skipping was "a hack" that could not handle
strings containing `/*`, nested comments, or `#`/`--` comment syntaxes — which is
exactly why the tokenizer (Stage 1) is a proper layer, not a regex patch.

We also studied the wider open-source field (Semgrep, CodeQL, SonarQube,
Bandit, Brakeman, gosec, Bearer, ...) to steal wins and avoid known dead ends —
see `docs/design/lessons-from-existing-tools.md`. The headline lesson, from
Semgrep's own authors, bounds this ADR: heavyweight interprocedural analysis is
slow, late, and high-false-positive because the underlying problem is
undecidable ("no amount of engineering will solve the halting problem"); a
fast, high-signal, low-false-positive pattern tool that runs pre-commit is the
better product. KEUR grows toward structure and flow only as far as that
signal-to-noise stays favourable — never toward a full prover.

Guiding principle (kept verbatim from the review because it is exactly right):

> Parse once. Index once. Filter cheaply. Match precisely. Report
> deterministically.

Design constraints we bind ourselves to:

- **Pragmatic over academic.** No factories/strategies/visitors/managers unless
  a concrete need forces them. Rules stay dumb data; the engine stays small.
  `struct Rule { id; metadata; expression; }`, not a class hierarchy.
- **Language-neutral rules, language-specific adapters.** A rule expresses a
  concept (`category: command-execution`); adapters map it to concrete tokens
  per language (`system`/`popen` in C++, `os.system`/`subprocess` in Python,
  `Runtime.exec` in Java). C++ is the first and richest adapter; others attach
  cheaply later.
- **Config/data/markup are not programming languages.** YAML/JSON/TOML/XML and
  Dockerfile/Terraform/K8s are handled as document analyzers, kept out of the
  code semantic model.
- **Discrete confidence**, not false precision: `certain | high | medium | low`
  mapped onto KEUR's existing block/warn bands — no invented `0.873`.
- **Dependency inversion at the seam:** rules depend on a `CodeModel`, never on
  a specific parser, so tree-sitter / clang / a hand tokenizer are swappable.

### Staged roadmap

- **Stage 0 (done):** RE2 line engine (ADR-001), provider architecture (ADR-003),
  `.keur/rules` cascade, the learn loop.
- **Stage 1 — tokenizer + comment/string awareness (in progress).** Adopt cpm's
  language tokenizer (`util/tokenizer`, from cpm `adr-165`), add `skip_comments`
  / `skip_strings` rule fields, scan stripped content when requested. Kills the
  comment/string false-positive class. This is "lex" from the pipeline.
- **Stage 2 — block/scope engines.** Adopt cpm's `block-absence` /
  `block_inner` / `block_trigger`, `scope` (line ranges), `extract`, and the
  file-level engines (cpm `adr-166`). Gives cross-line matching (e.g. "a test
  block with no assertion", "a function that opens a file but never closes it")
  without a parser — brace counting on stripped content.
- **Stage 3 — light structural index.** Not a full AST: a practical model
  (TranslationUnit → Namespace/Class/Function/Include/Macro; Function → name,
  params, calls, body, location) sufficient to write structural rules. C++
  first.
- **Stage 4 — source/sink/sanitizer flow.** A first-class flow rule type
  (`from: external-input`, `to: sink`, `not-through: sanitizer`) over the
  structural index, with the language adapters classifying sources/sinks.
- **Stage 5 — expression DSL.** `all/any/not/node/name/argument/inside/...` as
  the rule matcher language, so new structural rules need no C++.
- **Stage 6 — language adapters + inline code.** `LanguageFragment` /
  `ExecutionBoundary`: nest shell/python/SQL/regex/template fragments inside a
  host language and let one flow rule traverse the boundary. Prioritise embedded
  shell, SQL, regex, templates, JavaScript, Python, PowerShell.

Each stage is a separate unit of work with its own tests; later stages are only
started once the earlier ones are green and have earned their keep on the corpus.

### Positioning: not "another Semgrep" — the distillation layer above the field

An explicit strategic bound, because the natural temptation is to try to
out-detect Semgrep/CodeQL. We will not, and should not:

- Those tools win on DETECTION DEPTH (30+ parsers, generic AST, interprocedural
  taint, thousands of hand-written rules, years of work). Racing them there is a
  losing game — and per the field study, even commercial tools hit the
  halting-problem wall. KEUR is a fast, deterministic, pre-commit RE2 pattern
  engine; it will not match their depth and must not pretend to.

- What KEUR has that NONE of them has is the **learn layer**: a loop that
  proposes rules, measures them against ground truth, and gates them
  deterministically (with the slop/perturbation filter and composite-candidate
  store). Semgrep, CodeQL, Bandit, gosec, cpm, the AWS Security Agent — each is a
  fixed detector. KEUR is the layer that **learns from all of them at once**.

So the unique, defensible thesis is distillation, not competition:

```
  semgrep ┐
  codeql  ┤
  bandit  ┼─► ground truth (what the proven tools find, per adapter)
  gosec   ┤            │
  cpm     ┤            ▼
  aws-sec ┘   keur learn loop: propose → measure → GATE → slop-filter
                        │
                        ▼
        ONE fast deterministic RE2 rule-set that reproduces the UNION
        of what the field detects — minus each tool's setup/latency,
        minus the community-rule noise the gate rejects.
```

KEUR becomes the **meta / distillation / quality layer over the ecosystem**:
- broader than any single tool, because it learns from all of them (multi-source
  ground truth: groundtruth-cpm, groundtruth-awssec, and planned bandit/gosec/
  brakeman/gitleaks adapters);
- faster at run time, because the distilled output is one RE2 pass, not N tool
  invocations with N toolchains;
- self-improving and self-cleaning, because the gate + holdout + slop test
  reject the high-false-positive rules the field study flags as the main problem
  with community rule sets.

Honest limit: KEUR reproduces the PATTERNS the other tools surface, not their
graph analyses. A cross-file taint result from a semantic engine is not a
pattern and cannot be learned into an RE2 rule. So KEUR is "more than Semgrep"
in BREADTH, SPEED and SELF-IMPROVEMENT — never in interprocedural DEPTH. Keep
that line bright.

## Consequences

- KEUR keeps a **fast line-scan core**; heavier passes are opt-in per rule
  (`engine:`/`skip_comments:`/flow rules), so the common path stays sub-second
  and the pre-commit promise holds.
- The `.rule` format **grows additively** and stays cpm-compatible; cpm's richer
  corpus (block/extract/skip rules) becomes reusable as those engines land.
- Real risk of scope creep. Mitigation: this ADR is the contract that each stage
  ships and proves itself before the next begins; we do NOT build the semantic
  model or flow engine speculatively. If a stage does not earn measurable
  detection gains on the corpus, we stop there — the line engine remains a
  complete, useful product on its own.
- System-facing passes (tokenizer already; structural parsing later) must sit
  behind adapters (ADR-003) so platform/parser choices stay swappable and
  testable.
- Supersedes nothing: ADR-001's line engine remains the substrate. This ADR
  layers on top of it.
