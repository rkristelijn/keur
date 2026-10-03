# ADR-025 — Parallel `keur scan` across cores (deterministic, `scan` only)

@id ADR-025
**Status**: proposed · **Date**: 2026-09-23 · **Coverage**: none · **Relates to**: ADR-001 (RE2 — thread-safe for matching, the enabler), ADR-002 (single binary — threads, not processes), ADR-024 (CPU reduction — the work we parallelise), ADR-005 (platform abstraction — threading lives behind the seam)

## Context

`keur scan` is CPU-bound (see ADR-024 for the measured breakdown: `user` 3.62s
of 3.73s wall-clock on the `work-epub` benchmark; `sys` ~0.07s). When `user` ≈
`real` on a single thread, the workload has headroom to spread across cores:
on an 8-core machine the theoretical floor is roughly `real / cores`.

The scan loop in `scan_impl` (`src/rules/rule/scan.cpp`) is
**embarrassingly parallel** per file:

- Each file is read and matched independently — no cross-file state during the
  per-file pass.
- RE2 is **thread-safe for matching**: a compiled `RE2` (or `RE2::Set`) is
  read-only after compile and can be shared across threads. The compiled rule
  set is built once, then only read.
- The only shared mutation is `findings.push_back(...)`.

Two forces:

1. **We want throughput for `keur scan`** (full-repo, ad-hoc, CI full scans),
   where thousands of files each carry independent match work.
2. **`keur check` is different.** The hook scans only the *staged* diff — a
   handful of files. Thread spawn + join overhead would not pay back at that
   size, and the hook's priority is low latency and determinism, not throughput.

Constraint from ADR-002: one binary, no runtime plugins — so parallelism is
**in-process threads**, never the old per-rule process spawning (ADR-001's
rejected shell model). Constraint from ADR-005: OS-divergent code lives behind
`common/platform.*`; std C++ threading is portable, but any OS-specific
core-count / affinity detail stays behind the seam.

## Decision

Parallelise the per-file loop of `scan_impl` across a bounded thread pool, for
`keur scan` only, with determinism preserved by construction.

### 1. Partition by file, merge by order

- The file list is already gathered and **sorted** up front
  (`rules_scan_dir` does `std::sort(files...)`). Keep that.
- Distribute files across `min(hardware_concurrency, files)` worker threads.
- Each worker writes into its **own local findings buffer** — no shared
  container, no lock on the hot path.
- After join, **concatenate the per-file findings in the original sorted file
  order**, then within a file in the existing emit order. Output is therefore
  byte-identical to the single-threaded run: same findings, same order, same
  verdict.

### 2. Share compiled rules read-only

The compiled `RE2` / `RE2::Set` objects (built once in `scan_impl`) are shared
`const` across workers. RE2 matching is thread-safe; nothing mutates them during
the scan. No per-thread recompile.

### 3. `check` stays single-threaded

`keur check` (the hook) does **not** use the pool. The staged file set is small;
serial is simpler, lower-latency, and equally deterministic. This is an explicit
boundary, mirroring ADR-024's "the hook inherits the cheaper work, not the
threading".

### 4. Bounded and configurable

- Default pool size = detected core count, capped so a huge repo doesn't
  oversubscribe.
- A `KEUR_SCAN_THREADS` env / config knob allows pinning to 1 (reproduce serial
  behaviour exactly, for debugging) or a fixed N (CI reproducibility).
- Below a small file-count threshold, run serially — the pool never costs more
  than it saves.

## Why not the alternatives

- **Parallelise `keur check` too.** Too few staged files; spawn/join overhead
  outweighs the work. Latency and determinism matter more there. Rejected.
- **Process-per-rule / process-per-file (the old shell model).** Rejected by
  ADR-001/002: process spawn cost was the original minutes-long slowness, and it
  breaks the single-binary model.
- **Lock-protected shared findings vector.** Correct but contended on the hot
  path, and it makes output order depend on thread scheduling — a determinism
  hazard. Per-thread buffers + ordered merge avoids both.
- **Parallelise before reducing CPU cost (ADR-024).** You'd be spreading
  redundant per-rule passes across cores. Reduce the work first, then divide
  what remains. (Either order is correct; this is the recommended sequence.)
- **async / thread-per-file.** Thousands of threads thrash; a bounded pool sized
  to cores is the right granularity for a CPU-bound job.

## Consequences

- `keur scan` wall-clock drops toward `serial_user / cores` on multi-core
  machines; the benchmark (`work-epub`) is the acceptance measure — re-run it at
  1, 2, 4, 8 threads and record the scaling.
- **Determinism is preserved and testable**: `KEUR_SCAN_THREADS=1` must produce
  output byte-identical to today, and any N must produce output byte-identical
  to N=1 (diff the JSONL). This is the gating test, not a nice-to-have.
- Introduces threading into the engine — a new class of concurrency bug. Mitigated
  by: read-only shared compiled rules, no shared mutable state on the hot path,
  and the byte-identical-output test as a hard gate. Run the suite under a thread
  sanitizer in CI for this path.
- Threading primitives are standard C++ (`std::thread` / a small pool); any
  OS-specific core detection stays behind `common/platform.*` (ADR-005). Still
  one binary, still no runtime plugins (ADR-002).
- The hook (`keur check`) is unchanged and stays sub-second by staying serial on
  a small input — the parallel path is purely additive for full scans.
