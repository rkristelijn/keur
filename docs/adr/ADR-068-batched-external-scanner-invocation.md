# ADR-068 — Batched external-scanner invocation + merge-aware scoping for `keur check` on large staged sets

@id ADR-068
**Status**: proposed · **Date**: 2026-11-02 · **Coverage**: none · **Relates to**: ADR-019 (external scanners & project-local adapter — the per-file dispatch this changes), ADR-024 (scan CPU reduction — complementary, but that lever targets the *internal* RE2 engine, not external-tool spawns), ADR-025 (parallel scan — explicitly keeps `keur check` single-threaded on a *small* staged set; this ADR addresses the case where that "small" assumption breaks), ADR-066 (external scanners are opt-in — so this only bites repos that opted in)

## Context

`keur check` (the git hook) runs the external-scanner adapters over the staged
diff. In `scan/dispatch.cpp`, `run_adapter` invokes each enabled tool **once per
staged file**:

```cpp
for (const auto& file : filter_files(s.name, ctx.files)) {
    auto argv = build_argv(s, tool_config, file);   // one tool process ...
    auto res  = ctx.proc.capture_argv(argv);        // ... spawned PER FILE
}
```

This is cheap when the staged set is a handful of files — the assumption ADR-025
makes explicit ("`keur check` ... the staged file set is small; serial is
simpler, lower-latency"). It degrades badly when the staged set is large. The
triggering case: a **merge commit**. `git` marks the entire merged result as
staged, so a branch 15 commits behind `main` produced an **85-file** staged set,
and the hook "sat pumping" for well over a minute.

Measured per-spawn startup cost of the wired adapters (macOS, warm):

| tool       | startup / invocation | notes                          |
|------------|----------------------|--------------------------------|
| gitleaks   | ~0.07s               | cheap                          |
| shellcheck | ~0.48s               | moderate                       |
| **semgrep**| **~1.50s**           | Python runtime + ruleset load  |

The cost is **linear in staged-file count with a large per-file constant**. With
semgrep, ~60 scannable files × 1.5s ≈ **90s of pure process startup**, before any
matching. At a normal 3-file commit the same design is ~4.5s and nobody noticed —
which is why the per-file model survived.

Two distinct problems, two distinct causes:

1. **Per-file spawning of a high-startup tool does not scale with file count.**
   This is the root performance bug. It is *not* the CPU-bound internal-engine
   cost ADR-024 optimises — it is process-spawn overhead in the external-tool
   layer (ADR-019), a lever neither ADR-024 nor ADR-025 touches.
2. **A merge commit re-gates work that was already gated.** The incoming commits
   passed the gate when they landed on `main`. Re-scanning all 85 files on the
   merge commit is redundant by construction.

Two non-negotiable constraints carried from the engine's identity:

- **Determinism** (ADR-001, ADR-024, ADR-025): the verdict and findings must be
  byte-identical regardless of batching or scoping. No reordering that changes
  output, no probabilistic skipping.
- **Latency is the hook's whole point.** The fix must make the common case stay
  sub-second and the large case bounded — without a non-deterministic scheduler.

## Decision

Two independent, composable changes behind the existing dispatch, plus a static,
layered config. No adaptive runtime scheduler (see Why-not).

### 1. Batch external-tool invocation (root fix)

Invoke each external tool **once over the full (filtered) staged file list**
instead of once per file. gitleaks, semgrep and shellcheck all accept multiple
paths in a single invocation. `run_adapter` builds one argv with N files; the
output mapper (`map_*_json`) already carries the file path in each tool's JSON,
so finding attribution is recovered from the output, not from the invocation.

- Collapses `N × startup` into `1 × startup` per tool. ~60 semgrep spawns (~90s)
  → one (~1.5s + match).
- Determinism preserved: findings are sorted by `(file, line, rule)` after the
  single invocation, identical to the concatenation the per-file loop produced.
- Scope: the external-tool layer only. keur's internal rule engine is unchanged
  (ADR-024 governs that path).

### 2. Merge-aware scoping (`keur check`)

When a merge is in progress (`.git/MERGE_HEAD` present), the staged scope becomes
**only what this developer's side contributes**, not the entire merged tree,
because the incoming commits were already gated on their way into the target
branch. The only *new* decisions in a merge are this developer's.

Defining "this developer's contribution" correctly matters — a first attempt
used `git diff --cached --diff-filter=M` and it was **wrong in the real case**:
on a wide merge (both sides touched many files) git marks ~50 files as Modified,
most of them files the developer did not meaningfully change. Measured on the
container-images merge that motivated this ADR: `--cached --diff-filter=M`
returned 50 files (still full of other teams' pre-existing findings), while the
**three-dot diff against the merge-base** — `git diff --name-only
<merge-base>...HEAD`, i.e. what this branch added relative to where it forked —
returned **exactly the 9 files the developer actually changed**. Every blocking
finding sat in the 41-file difference (incoming work), none in the 9.

So the scoped set is the **merge-base three-dot diff** (`git merge-base HEAD
MERGE_HEAD` → `git diff --name-only <base>...HEAD`), intersected with the staged
set so only files actually part of this commit are scanned. This is the honest
"what am I, the committer, responsible for" set. `--diff-filter=M` alone is
rejected: it conflates "both sides touched it" with "I changed it".

The scope is selectable (see §3 `merge_scope`): `resolved` (default, the
merge-base diff), `all` (whole staged tree — today's behaviour), `skip` (nothing
during a merge — the incoming work was already gated).

### 3. Static, layered configuration (sensible default → global/repo `keur.toml` → one-shot override)

Mirrors the existing config stack (`block-at`, `KEUR_SCAN_THREADS`):

```toml
[performance]
batch = true          # external-tool batching (default on)

[pre_commit]
merge_scope = "resolved"   # resolved (default) | all | skip
max_files   = 0            # 0 = no cap; a last-resort guard, not the fix
```

- **Default** in code (batch on, merge_scope=resolved).
- **`keur.toml`** overrides per repo.
- **One-shot override** via env/flag: `KEUR_BATCH=0`, `KEUR_MERGE_SCOPE=all`,
  `--merge-scope all` — for the one time you want it different (debugging a
  batched tool, or force-gating a whole merge).

## Why not the alternatives

- **`max_files` cap as the primary fix.** Rejected as a fix (kept only as an
  optional last-resort guard). It hides the symptom — a legitimate 85-file
  feature commit would be silently under-gated. Batching removes the need.
- **Adaptive auto-tuning scheduler** (measure the machine, then dynamically pick
  parallel/serial/batch/in-memory per run). Rejected for a *gate*:
  non-determinism is gift-wrapped poison ("why did it block last time but not
  now?"), it adds measurement overhead to every hook run, and it is a large,
  bug-prone machine for marginal gain over static batch+config. We adopt the
  *good* half — **measurement as a diagnostic** (`keur doctor`-style timings so a
  human chooses), not as a runtime controller.
- **Parallelise `keur check`.** Already decided against in ADR-025 for the
  small-staged-set case; with batching the large case no longer needs it either
  (one spawn per tool, not N). If a future need arises it belongs under ADR-025's
  model, not here.
- **Read files into memory / in-memory pipe to tools.** ADR-024 already rejected
  the in-memory idea for the internal engine (CPU-bound, `sys` ~2%). External
  tools read their own files from disk by path; batching the *paths* is the win,
  not batching reads.
- **Skip external tools entirely on a merge.** Too blunt — a conflict resolution
  can introduce a real secret or issue. Scope to the resolved files instead.

## Consequences

- `keur check` on a large staged set drops from `N × tool_startup` toward
  `1 × tool_startup` per tool. The merge case goes from ~90s to ~seconds; the
  common small commit is unchanged (already fast).
- Determinism is the acceptance gate, same as ADR-024/025: batched output must
  diff byte-identical to the per-file output on a fixed corpus; `merge_scope=all`
  must reproduce today's whole-merge behaviour exactly. These are the tests, not
  nice-to-haves.
- Mapper robustness becomes the real implementation risk: each `map_*_json` must
  attribute findings to the correct file from a multi-file tool run. This is the
  focus of the unit tests (fixtures with findings across several files).
- Small added surface in `dispatch.cpp` (batched argv + per-tool output split)
  and `check.cpp` (merge detection + scope selection) + config parsing. The
  engine registry (ADR-014) and rule semantics are untouched.
- **Supersession**: this ADR does not supersede ADR-019/024/025; it fills a case
  they left open (the hook on a non-small staged set). Should a later ADR replace
  this batching model, mark it here as *superseded by ADR-NNN* and add the
  reciprocal *supersedes ADR-068* on that ADR, per the repo convention (cf.
  ADR-018 ↔ ADR-028).

## Migration (test-first, per AGENTS.md)

1. Characterise current findings on a multi-file fixture (per-file path).
2. Implement batching; re-run and **diff findings byte-identical**, re-measure
   the 85-file case.
3. Add merge-aware scoping behind `merge_scope`; verify `all` == today, default
   `resolved` scans only the resolved set.
4. Wire the `[performance]` / `[pre_commit]` config + env/flag overrides; test
   the three layers resolve in the right order.
