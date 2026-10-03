# ADR-024 — Scan CPU reduction: combined-regex (`RE2::Set`) + file-level literal pre-filter

@id ADR-024
**Status**: proposed · **Date**: 2026-09-23 · **Coverage**: none · **Relates to**: ADR-001 (the RE2 line engine this optimises), ADR-007 (pattern `exclude` — interacts with combined matching), ADR-014 (engine registry — where combined matching plugs in), ADR-025 (parallel scan — the complementary lever)

## Context

`keur scan` is **CPU-bound, not I/O-bound**. Measured on a large real repo
(`work-epub`, 4698 files scanned, ~29.8 MB, 242 active rules, warm cache):

| metric | value | reading |
|--------|-------|---------|
| `real` | 3.73s | wall-clock |
| `user` | **3.62s** | CPU in userspace — regex matching |
| `sys`  | **0.07s** | kernel / file I/O |

`user` is **97%** of wall-clock; `sys` is **~2%**. Three back-to-back runs were
identical (3.75 / 3.75 / 3.87s), so this is warm-cache — disk is not the
bottleneck. Two control measurements confirm the shape:

- A near-empty directory scans in 0.07s (0.03s `user`) — **startup and the
  compile of 242 rules to RE2 cost almost nothing**. The ~3.6s is pure match
  work.
- A single 785 KB file costs 0.16s — the per-byte match cost, which scales with
  `bytes × applicable_rules`.

This kills the intuitive "read the whole tree into memory" idea: we are not
waiting on I/O, so batching reads saves nothing. It would also *lose* the
current property that a file matched by no rule (`candidates.empty()`) is never
read at all.

The real cost model is **`bytes × applicable_rules_per_line`**. Today, in
`scan_impl` (`src/rules/rule/scan.cpp`), each applicable rule runs its **own**
RE2 match, per line, per file. With 242 rules and many rules sharing an
extension bucket, a single line can be walked by dozens of independent RE2
automata. That is the dominant term.

Two forces:

1. **The absolute CPU cost must come down.** This is the term that also lands in
   `keur check` (the hook), where latency matters most. Lowering it helps
   *every* invocation, staged-diff or full-repo.
2. **Determinism is non-negotiable** (ADR-001, the whole identity). Any speedup
   must produce byte-identical findings, same order, same verdict. No
   probabilistic pruning, no reordering that changes output.

## Decision

Two independent, composable optimisations, both behind the existing dispatch so
the engine registry (ADR-014) and rule semantics are untouched.

### 1. File-level literal pre-filter (cheap, do first)

Before running a rule's per-line RE2 over a file, test the file's whole content
against a cheap **required literal** derived from (or declared on) the rule. If
the literal is absent from the file, the rule cannot match — skip all its
per-line work for that file.

- Extends the existing `content_contains` pre-filter (ADR-001) from a hand-set
  field to an **auto-derived required substring** where a regex has an anchoring
  literal (e.g. `AKIA` for an AWS key, `-----BEGIN` for a PEM block,
  `password` for a credential rule).
- Derivation is conservative: only a literal that MUST appear on any match is
  used (a mandatory, non-optional substring of the pattern). When no such
  literal exists, the rule runs unfiltered exactly as today — no behaviour
  change, no risk of a false negative.
- One `content.find()` over the file buffer is orders of magnitude cheaper than
  a per-line RE2 pass, and on document-heavy trees (like the benchmark) it
  serves out the large majority of secret/code rules immediately.

### 2. Combined matching with `RE2::Set` (structural, do second)

Replace N independent per-line RE2 matches with **one combined automaton** per
dispatch bucket. RE2 supports compiling many patterns into a single `RE2::Set`
that reports, in one linear pass over the text, **which** of its members
matched.

- Per extension/filename bucket (the existing `ext_index` / `name_index`), build
  a `RE2::Set` of the surviving `pattern`-engine regexes. One pass over a line
  yields the set of matching rule-ids instead of one pass per rule.
- Finding attribution (which pattern → which rule, line, message) is recovered
  from the set-member index back to the owning `Compiled` rule, so the emitted
  findings are identical to today's.
- Scope: the `pattern` engine (the bulk of the corpus). Engines with per-match
  structure that `RE2::Set` cannot express — `exclude`/pattern-not (ADR-007),
  `block-absence`, `max-nesting`, `count` — keep their current per-rule match.
  They are the minority and are dispatched exactly as now via the registry.

### Order and rationale

Do the **pre-filter first** (small, local, immediately measurable, zero
semantic change), then **`RE2::Set`** (bigger structural change to matching).
Both lower the absolute CPU term, so their benefit is also inherited by the hook
and by the parallel path (ADR-025) — parallelising *less* work.

## Why not the alternatives

- **Read the whole tree into memory, then loop.** Rejected by the data: the
  workload is CPU-bound (`sys` ~2%), so batching reads saves nothing, costs RAM,
  and discards the "unmatched file is never read" pre-filter.
- **Probabilistic / sampling pruning.** Any pruning that could drop a real match
  breaks determinism (ADR-001). The pre-filter here is *exact*: it only skips
  work that provably cannot match.
- **Hand-write one mega-regex per bucket.** `RE2::Set` is the supported,
  maintainable form of exactly this — it keeps rules as independent data
  (ADR-009) while matching them in one pass.
- **A faster regex engine than RE2.** RE2's linear-time guarantee is the ReDoS
  defence and the determinism promise (ADR-001). Not on the table.

## Consequences

- Match cost drops from `bytes × applicable_rules` toward `bytes × buckets`
  (combined matching) minus the files the pre-filter serves out entirely. The
  win is largest exactly where the corpus is largest.
- The gain lands in **both** `keur scan` and `keur check` — the hook gets faster
  too, without any threading.
- Determinism is preserved by construction: identical findings, identical order,
  identical verdict. This is the acceptance test — the benchmark
  (`work-epub`, and the `eval/` ground truth) must produce the same findings
  before and after, only faster. Verify with a diff of `keur scan` JSONL output
  pre/post change.
- Small added complexity in the compile step (deriving literals, building the
  `RE2::Set` per bucket and the member→rule map). Contained in `scan.cpp` +
  `internal.h`; the registry (ADR-014) and the `.rule` format are unchanged.
- Migration is test-first (AGENTS.md): characterise current findings on the
  benchmark, apply the pre-filter, re-measure and diff findings; then the
  `RE2::Set` change, re-measure and diff again. The benchmark decides.
