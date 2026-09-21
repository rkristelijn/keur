# Scanner dispatch — implementation plan

@id scanner-dispatch-plan
**Date**: 2026-09-20 · **Status**: plan (not yet built) · **Implements**:
ADR-019 (external scanners & project-local adapter), ADR-003 (provider/adapter),
ADR-014 (policy is the sole referee) · **Tracks**: TODO.md "libmode" item.

## Goal (the user's daily flow)

A single `keur check`, run by the global pre-commit hook on every commit, that
gathers evidence from three layers **in this order** and feeds all of it through
the one policy engine:

```text
global hook (pre-commit)
  1. established tooling   → semgrep, gitleaks, trivy, ... (proven-tool adapters)
  2. keur rules            → global → local, resolved on specificity
  3. project-local check   → git-tracked .keur/check.sh (ADR-019 trust model)
  → all findings → policy_evaluate() → PASS / WARN / BLOCK
```

The order is for **presentation and fail-fast**, not for the verdict: the policy
engine evaluates the aggregated findings once, and the source only sets
confidence, never the verdict (ADR-014 invariant).

## What already works (verified 2026-09-20)

- **Layer 2 (keur rules) is complete.** `util/keur_utils.h::rule_dirs()` resolves
  general → specific exactly as required: `$KEUR_HOME/rules` → `$XDG_CONFIG_HOME/
  keur/rules` (global/company) → nearest `<git-root>/.keur/rules` (project/local,
  found by walking up like git finds `.git`) → `$KEUR_RULES_DIRS` (explicit
  override). Later layers augment/override earlier ones.
- **The global hook works.** `keur hook --global` sets `core.hooksPath`; the stub
  runs `exec keur check --tier pre-commit`.
- **The seams for external tools exist.** `adapters::Proc` provides `capture`
  (cmd → stdout+exit), `run_argv` (shell-free argv, CWE-78 safe), and `has`
  (PATH lookup). `adapters::Git::run` can answer `ls-files --error-unmatch`
  (tracked check). All spawning is centralised (ADR-003).

## The gap (the only real work)

`cmd/check.cpp::check()` today dispatches **only** the built-in RE2 rule engine
(layer 2). The whole `[scanners.*]` inventory in `config/keur.toml` (gitleaks,
semgrep, trivy, and the `pii` script-scanner) is **config ahead of code** — no
external-tool or script scanner is invoked yet. Layers 1 and 3 do not run.

So the prerequisite for both proven-tool adapters (ADR-019 shape A) and the
project-local adapter (shape B) is a **scanner-dispatch step** inside `check()`.

## Design

### Finding — the common currency

Every layer produces the canonical Finding shape (already emitted by the rule
engine and defined for adapters in ADR-019):

```
source, rule, title, category, severity, action, confidence, deterministic,
file, line, message
```

`policy_evaluate()` stays a pure function over aggregated Findings. Today it
takes a `PolicyInput` of booleans; it will instead fold over a
`std::vector<Finding>` (or the booleans are computed from that vector at the
call site to keep the pure core unchanged in step 1 — see phasing).

### New component: `scan/dispatch` (pure orchestration over injected seams)

A small unit that, given the staged files, the resolved scanner config, and the
adapter seams (`Proc`, `Git`), returns `std::vector<Finding>`. No I/O of its own —
it calls the injected `Proc`/`Git`, so it is table-testable with fakes (the
established pattern: tests assign the `std::function` members).

```
Findings dispatch_scanners(files, scanners_cfg, Proc, Git)
  for each scanner in tier order:
    - proven-tool adapter (bin on PATH):
        Proc.has(bin) ? run + normalise(stdout) : skip   // missing → skip
    - keur-rules:
        existing rules_scan_files(rules, files)           // layer 2, unchanged
    - project-local adapter:
        resolve .keur/check.sh then .config/keur/check.sh
        run ONLY if Git says it is tracked (see trust model)
        Proc.run_argv({script, files...}) → parse JSONL → cap confidence
```

### Per-tool normalisation

Each proven tool emits its own JSON; a small per-tool mapper turns it into
Finding. First adapter: **shellcheck** (`--format=json1`), mapping SC-codes with
sane `action`/`confidence` (SC2034 → `warn`, not `block`, per ADR-019 open
question). One mapper function per tool, each independently testable with a
captured sample of that tool's output.

### Project-local adapter — trust model (decided, ADR-019)

- Resolution order: `.keur/check.sh` then `.config/keur/check.sh`.
- **Run only if git-tracked** (`git ls-files --error-unmatch <path>` succeeds).
  Untracked/dropped scripts are ignored — rides git's existing trust boundary.
- **Shell-free** spawn via `Proc::run_argv({script, file1, file2, ...})`.
- Reads KEUR Finding JSONL from stdout.
- **Confidence cap** (proposed 0.80, below every calibrated proven-tool source)
  so a repo cannot self-BLOCK above proven evidence.
- Config `[scanners.project-local] enabled = false` disables it entirely.

### Ordering / budget

- Order 1→2→3 is presentation + optional fail-fast. Decision to confirm before
  wiring: **fail-fast** (stop at first `action=block`, skip the rest — fastest in
  the 3000 ms pre-commit budget) vs **full-report** (always run all, one complete
  report). Default proposal: **full-report on `scan`, fail-fast on `check`** so the
  gate stays fast but ad-hoc inspection stays complete.
- Tier placement per scanner from `keur.toml`: heavy tools (semgrep, trivy) may
  move to `pre-push`/`ci` if they blow the pre-commit budget. Measure first.

## Phasing (test-first — non-negotiable per AGENTS.md)

1. **Finding aggregation seam.** Refactor `check()` so scanner results are
   collected into a `std::vector<Finding>` and `PolicyInput` is derived from it.
   Characterise with existing tests first; keep `policy_evaluate` pure and green.
2. **Dispatch skeleton + keur-rules through it.** Move the current rule-engine
   call into `dispatch_scanners()`; behaviour identical, now injectable. Tests:
   same verdicts as before (regression), plus dispatch-level tests with a fake
   Proc/Git.
3. **First proven-tool adapter: shellcheck.** Config entry + `--format=json1`
   mapper + PATH-skip. Tests: captured shellcheck sample → expected Findings;
   missing binary → skipped (fake `Proc.has` = false).
4. **Project-local adapter (shape B).** Resolve path, tracked-check via fake Git,
   `run_argv` via fake Proc, JSONL parse, confidence cap. Tests: tracked script
   runs and findings flow to policy; untracked script ignored; `confidence=1.0
   action=block` from the script is capped so it WARNs but does not solo-BLOCK.
5. **More proven-tool adapters** (gitleaks, semgrep, trivy, checkov) as separate
   mappers, each with a captured-sample test. Reuse the skip-on-missing path.
6. **Budget pass.** Measure pre-commit wall time with tools present; move heavy
   tools to pre-push/ci in `keur.toml` if over budget. Confirm fail-fast vs
   full-report choice with a real run.

## Verification (after each phase)

- `cd src/rules && make && cd ../keur && make test` — build both, tests green.
- `eval/doc-coverage.sh` — no new dead links / untraced concepts; annotate new
  units with `@implements ADR-019` / `@proves`.
- keur's own self-check rules (`KEUR-ARCH-*`/`KEUR-PORT-*`) stay green — I/O only
  in `adapters/`, dispatch logic pure, no new `#ifdef`.

## Invariants (do not break)

- Policy engine is the sole referee (ADR-014). Adapters produce evidence only.
- One binary, no runtime plugin system (ADR-002). The project-local adapter is
  one fixed, sandboxed entry point, not a plugin loader.
- keur does not reimplement linters (ADR-001). It shells out and normalises.
- I/O stays in `adapters/`; the dispatch decision logic is pure (ADR-003).

## Open decisions to confirm before building

1. Fail-fast vs full-report on `check` (proposal above).
2. Exact confidence cap for project-local findings (proposed 0.80 — verify
   against the corroboration math so a capped finding can WARN but never
   single-handedly BLOCK above a proven tool).
3. Which proven tools belong in `pre-commit` vs `pre-push` under the 3000 ms
   budget (measure, don't guess).
