# ADR-028 — Sunset cpm; keur is the successor (hook + pipeline)

@id ADR-028
**Status**: accepted · **Date**: 2026-09-25 · **Relates to**: ADR-001 (keur's engine is a clean-room of cpm's design), ADR-018 (cpm as a second gate — now superseded), ADR-019 (external-tool scanners), ADR-026 (rule maturity), ADR-012 (pipeline coverage)

## Context

cpm (github.com/rkristelijn/cpm) and keur are siblings with the same anti-slop
goal. keur's rule engine is already a clean-room re-implementation of cpm's
design (ADR-001) and the `.rule` format is cpm-compatible. Verified compatibility
(2026-09-25):

- **Engines**: cpm uses `pattern, presence, absence, file-absence, file-presence,
  block-absence, extract-duplicates` — keur supports all of these (plus `count`,
  `multiline`, `max-lines`, `max-nesting`, `max-dir-files`, `path-derived`,
  `composite` that cpm lacks). No cpm rule uses an engine keur cannot run.
- **Fields**: cpm's rule keys (`id, title, category, severity, engine, fix,
  target{extensions,filenames,exclude_paths,content_contains}, patterns{message},
  skip_comments, skip_strings, message, extract, capture, block_inner,
  block_trigger, scope`) are all understood by keur's parser — EXCEPT
  `exclude_names:` (3 cpm rules use it), which keur silently ignores today.
- cpm's ≈1004 rules are format-loadable by keur.

But keur's founding lesson from cpm (docs/design/lessons-from-cpm.md) is:
**"Generation outran validation. CPM's rules were essentially all machine-
generated, and validation never went beyond CPM's own repository."** keur exists
to do the boring, decisive work cpm skipped: a benchmark corpus, measured
precision, a validation gate. So keur does not "sync cpm's ruleset" — it puts
candidate rules through its eval loop and keeps what measurably helps.

The two tools also differ in **where they run**:

- **keur**: built as a git pre-commit / pre-push hook — must be sub-second on the
  staged diff (README "More than a hook", ADR budgets).
- **cpm**: built to be run ad hoc and **in the CI pipeline** over the whole tree.

## Decision

1. **Sunset cpm; keur is the successor.** New quality/security rule work lands in
   keur. cpm goes into maintenance/archive. ADR-018 (cpm as a second gate over
   keur) is **superseded** — keur gates itself.

2. **Do NOT bulk-import cpm's 1004 rules.** They are largely unvalidated,
   machine-generated (the exact failure keur was built to avoid). Instead:
   - Import candidates **through the eval loop**: a cpm rule is adopted only when
     it raises precision/recall on the ground-truth corpus (the keur way).
   - Tag adopted rules `source: cpm` (the provenance field already exists) so
     origin, upstream-sync and corpus-exclude stay traceable.
   - Assign `maturity:` on import (ADR-026), and put framework-specific rules
     (flask/vue/mui/…) behind a `requires: framework:` guard + high maturity —
     never flat on the default band.
   - Add `exclude_names:` to the parser so the last 3 cpm rules load 1:1 (small,
     data-only engine extension; no new engine).

3. **Make keur run in the pipeline, not only as a hook.** cpm's pipeline role is
   the one capability keur must grow to fully replace it. Two concrete gaps:

   a. **A real `ci` tier in `keur check`.** `config/keur.toml` already declares
   `[tiers.ci] budget-ms = 0` with the full scanner set (gitleaks, trufflehog,
   trivy, grype, osv-scanner, semgrep, checkov, tfsec, keur-rules), but
   `check_parse` only knows `pre-commit`/`pre-push` (anything else falls back
   to pre-commit). Extend the tier enum + parser with `ci`: no time budget,
   run the heavy corroborating tools, scan the **whole tree** (not just the
   staged diff), emit a machine-readable report, and exit non-zero on a
   blocking verdict so CI fails. Reuse the one policy engine (ADR-014) — the
   tier only changes which scanners run and the scope, never the verdict math.

   b. **A gate mode for `keur scan`.** `keur scan` already walks the whole tree
   (`rules_scan_dir`) but always exits 0 — it is a report, not a gate. Add a
   `--gate` (or reuse `check --tier ci`) that applies the policy engine to the
   whole-tree findings and returns the same PASS/WARN/BLOCK exit contract as
   the hook. This is what a CI job invokes.

## cpm's internal checks — where each one lands in keur

cpm also has ≈24 built-in `[checks]` (cpm.toml) beyond its `.rule` files. They do
NOT all become keur rules; they fall into four buckets. Most are proven-tool
adapters — exactly the scanner-dispatch built under ADR-019 — not rules or engine
work.

| cpm check | keur home | note |
|-----------|-----------|------|
| `code-generic-secrets-scan`, `sast-secret` | **adapter** (ADR-019) | gitleaks — already wired |
| `code-generic-vulnerability-scan`, `sast-security` | **adapter** | semgrep/trivy — already wired |
| `code-scripts-syntax-lint`, `lint-scripts` | **adapter** | shellcheck — already wired |
| `code-cpp-syntax-lint`, `lint-tidy` | **adapter** | clang-tidy |
| `code-cpp-quality-lint` | **adapter** | cppcheck |
| `lint-makefile` | **adapter** | checkmake |
| `code-yaml-syntax-format`, `format-yaml`, `lint` yaml | **adapter** | yamllint / prettier |
| `docs-markdown-syntax-format`, `format-md`, `docs` | **adapter** | rumdl / markdownlint (keur already uses rumdl) |
| `format-code` | **adapter** | clang-format (--check) |
| `[limits]` source/header/script-lines, files-per-dir | **already a rule** | `max-lines` (KEUR-QUAL-005), `max-dir-files` (KEUR-QUAL-006) |
| `comment-ratio` (threshold 20) | **small new engine** | per-file comment:code ratio; RE2-free, modelled on `max-lines`. |
| `complexity` (cyclomatic, threshold 10) | **engine tension (ADR-001)** | true cyclomatic needs control-flow, not regex. Either approximate via existing `max-nesting`, or orchestrate an external tool (cppcheck/lizard) as an adapter. Do NOT put an AST in the RE2 engine. |
| `code-cpp-comment-measure`, `*-complexity-measure` | **metric, not a gate** | these *measure*, they don't pass/fail. Surface via pipeline-coverage/metrics (ADR-012/017), not as a blocking rule. |

Rule of thumb (the ADR-019 guidance, restated): pattern-matchable → a `.rule`;
dataflow/AST/cross-file → orchestrate the proven tool as an adapter; a per-file
scalar (lines, ratio) → a small file-level engine; a number you report but don't
fail on → a metric, not a gate. Only `comment-ratio` clearly warrants new engine
work; `complexity` is deliberately delegated to a tool to preserve ADR-001.

## Consequences

**Positive**

- One tool, one rule format, one policy engine across commit → push → CI. The
  "passed locally, failed in CI" gap (the miss that motivated ADR-019) closes:
  the CI tier is a superset of the hook tiers, same engine.
- cpm's maintenance burden ends; its proven rules live on in keur, but only the
  measured ones — count no longer outruns validation.
- keur becomes usable as the pipeline quality/security gate cpm was, plus the
  hook it already is.

**Negative / risks**

- The eval-gated import is slower than a bulk copy. That is deliberate: it is the
  whole reason keur exists over cpm. Accept the slower, measured path.
- Heavy CI-tier tools (semgrep/trivy/grype) are PATH-dependent; missing tools
  skip gracefully (ADR-019), so CI without them still runs keur-rules.
- A whole-tree CI scan is slower than the diff-scoped hook — bounded by the
  `[tiers.ci]` budget (unlimited by design) and only run in CI, not on commit.

## Not doing

- Bulk-copying all 1004 cpm rules (violates the validation-first principle).
- Keeping cpm as a parallel second gate (ADR-018 superseded).
- Changing the verdict logic per tier — the policy engine stays the sole referee
  (ADR-014); tiers only vary scanner set + scope.

## Follow-ups (implementation, tracked separately)

1. `check`: add `Tier::Ci` (enum + `--tier ci` parse), whole-tree scope, full
   scanner set, non-zero exit on BLOCK, machine-readable report (JSON/SARIF?).
2. `exclude_names:` parser key (+ a `@proves` test on the 3 cpm rules).
3. Eval-loop import path for `source: cpm` candidates (measure → promote).
4. A CI recipe in the README: `keur check --tier ci` as a pipeline step.
5. Archive/maintenance notice on the cpm repo pointing to keur.
6. cpm's internal checks (table above): wire the adapter-class ones via
   scanner-dispatch (clang-tidy, cppcheck, yamllint, markdownlint, clang-format);
   add a `comment-ratio` file-level engine; decide `complexity` (approximate via
   max-nesting vs delegate to cppcheck/lizard); route the `*-measure` ones to
   metrics, not the gate.
