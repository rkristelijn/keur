# ADR-019 — External-tool scanners & a project-local check adapter

@id ADR-019
**Status**: proposed (trust model decided; not yet built) · **Date**: 2026-09-20 · **Coverage**: none · **Relates to**: ADR-001 (RE2 rule engine — why keur is not itself a linter), ADR-002 (single binary — no runtime plugins), ADR-003 (provider/adapter architecture), ADR-014 (engine registry), ADR-018 (cpm as a second gate)

<!-- Coverage: none — decision recorded, not yet built. Captures the pattern for
     letting proven external tools and per-project checks feed keur's verdict,
     motivated by a real miss (below). -->

## Context

keur's rule engine is a RE2 pattern matcher (ADR-001): fast, deterministic,
linear-time. It deliberately does **not** do dataflow/AST analysis or cross-file
resolution, and the config already frames keur's own rules as a *gap-filler* for
what proven tools cover ("KEUR's own rule engine is a gap-filler for what these
proven tools don't cover — never the primary layer", `config/keur.toml`).

The scanner abstraction to support this already exists: `[scanners.*]` entries
are thin adapters over proven tools (`gitleaks`, `semgrep`, `trivy`, `checkov`),
resolved on PATH, skipped gracefully when missing, emitting KEUR Finding JSONL
that the policy engine turns into PASS/WARN/BLOCK. One scanner (`pii`) is already
a **script** adapter (`bin = "pii-sensitive-scan.sh"`), proving the model handles
"a script that emits findings", not only compiled tools.

**Motivating miss (2026-09-20).** A consumer repo (`security-tooling`) uses keur
as its pre-commit hook. A change passed keur locally but the GitLab CI failed on:

1. `shellcheck` **SC2034** (unused variable) — a dataflow check keur's RE2 engine
   cannot do by design.
2. a **dead-links** check — cross-file link resolution keur does not do.

Root cause was **not** a keur bug: the local gate (keur rules) and the CI gate
(shellcheck + link check) had diverged. keur caught its class of things; the
other classes had no local gate. The lesson: keur should be able to *orchestrate*
these proven checks so the local verdict matches CI, without each repo hand-rolling
its own hook plumbing.

Two shapes were proposed:

- **A.** First-class external-tool scanner adapters (e.g. `[scanners.shellcheck]`),
  like `semgrep` — every repo of that language gets the coverage.
- **B.** A per-project escape hatch: keur runs a repo-provided `.keur/check.sh`
  (or `.config/keur/check.sh`) and consumes its findings — for checks too
  project-specific to live in keur (e.g. this repo's dead-links, structure rules).

## Decision (to make)

Adopt **both**, layered, keeping the Finding-shape contract as the invariant:

1. **External-tool scanner adapters (A).** Add proven linters as `[scanners.*]`
   adapters exactly like `semgrep`/`checkov`: PATH-resolved, missing → skipped,
   output normalised to KEUR Finding JSONL by a small adapter. First candidate:
   `shellcheck` (deterministic, ubiquitous, covers the SC2034 class). This is the
   preferred layer — coverage is shared across all repos, not opt-in per repo.

2. **Project-local check adapter (B).** A single reserved scanner
   (`[scanners.project-local]`, default enabled) that runs an executable at a
   well-known, **git-tracked** path in the scanned repo — resolution order:
   `.keur/check.sh` then `.config/keur/check.sh` — passing it the staged file
   list on argv (mirroring the existing script-scanner calling convention) and
   reading KEUR Finding JSONL from its stdout. The script is only run when it is
   a tracked file (see *Trust model* below), spawned shell-free, and its findings
   are confidence-capped. They flow through the **same policy engine**:
   `action=block` findings block the commit, others warn. No new verdict path.

**Invariant (non-negotiable):** adapters emit the canonical Finding shape
(`source, rule, title, category, severity, action, confidence, deterministic,
file, line, message`). The policy engine stays the sole referee (ADR-014). A
project script may *produce* evidence; it may never *decide* the verdict itself.
This is what keeps a project escape hatch from becoming an ungoverned side door.

### Boundaries

- keur does **not** reimplement shellcheck/link-checking logic in C++ (ADR-001).
  It shells out to the proven tool and normalises output — same as `semgrep`.
- The project-local adapter is a **thin runner + JSONL parser**, resolved in the
  existing scanner-dispatch; it is not a general plugin runtime (ADR-002 — no
  runtime plugin system; this is one fixed, sandboxed entry point).
- Tier placement: `shellcheck` and `project-local` belong in `pre-commit` only if
  they fit the 3000 ms budget; otherwise `pre-push`/`ci`. Measure before wiring.

### Trust model for `.keur/check.sh` (decided)

Running a repo-provided script on commit is code execution, so the question is
*who vouches for that script*. A config `enabled = false` default was considered
and **rejected**: it adds friction for every legitimate repo (including
`security-tooling`, the repo that motivated this ADR) and does not actually stop
the dangerous case. The dangerous case is not "keur is installed and I wrote a
check" — it is "keur is globally installed, I clone an untrusted repo, and a
dropped script runs on my first commit". The mitigation must target *that*.

Decision — **default enabled, but gated on git trust, not on config**:

1. **Tracked-only execution (the primary control).** keur runs the script only
   when it is a **tracked file in the repo** (`git ls-files --error-unmatch`
   succeeds). A present-but-untracked or freshly-dropped script is ignored. This
   reuses git's own trust boundary: if you trusted the script enough to commit
   it, you trust it enough to run it. A cloned repo's `.keur/check.sh` *is*
   tracked, so trust here rests on the same "do you trust this repo" judgement
   you already make when cloning and running its build — no new, wider surface.
2. **Shell-free invocation.** The script is spawned via `Proc::run_argv`
   (argv vector, no `/bin/sh`), so the staged-file list passed on argv cannot be
   reinterpreted as shell metacharacters (CWE-78, KEUR-SEC-006).
3. **Confidence cap (governance control).** Project-local findings are capped
   (proposed `0.80`, below the calibrated proven-tool sources) so a repo cannot
   mark everything `block` at `confidence=1.0` and override calibrated evidence.
   The policy engine stays the sole referee (the invariant above).
4. **Config override remains.** `[scanners.project-local] enabled = false`
   disables it entirely for operators who want the surface gone regardless.

Rationale for "why not opt-in by default": keur doing nothing when absent is
true but irrelevant — the risk lives on machines where keur *is* present and a
foreign repo is cloned. Tracked-only + confidence-cap closes that path while
keeping the motivating case (a repo committing its own dead-links / structure
check) working with zero configuration. This is "fix + control": it works out of
the box, and the control sits on git's existing trust boundary rather than a
flag people will forget to flip.

### Open questions

- **Finding normalisation effort per tool.** shellcheck emits `--format=json1`;
  mapping to the Finding shape is small but must set `action`/`confidence`
  sensibly (SC2034 as `warn`, not `block`, to avoid overblocking).
- **Exact confidence cap value.** `0.80` is proposed (below every calibrated
  proven-tool source in `config/keur.toml`); confirm against the corroboration
  math before wiring so a capped project finding can still WARN but never
  single-handedly BLOCK above a proven tool.

## Consequences

**Positive**

- Local verdict can match CI — closes the "passed locally, failed in CI" gap that
  motivated this ADR.
- Reuses the existing scanner abstraction (ADR-003) and policy engine (ADR-014);
  no new verdict logic.
- shellcheck-as-adapter benefits every shell repo; the project adapter absorbs
  the long tail without bloating keur's rule corpus.

**Negative / risks**

- More PATH-dependent tooling (mitigated: missing tools skip gracefully, as today).
- The project-local adapter is an execution surface. Mitigated by the *Trust
  model* above: tracked-only execution, shell-free spawn, confidence cap. Without
  those it would be an unreviewed side channel; with them it rides git's existing
  trust boundary.
- Two ways to add a check (a keur `.rule` vs. an external adapter) — guidance
  needed: pattern-matchable → `.rule`; dataflow/AST/cross-file → external tool;
  repo-specific → project-local.

**Implementation note (state of the code, 2026-09-20)**

- `config/keur.toml` already lists `[scanners.*]` (gitleaks, semgrep, pii, …),
  but `cmd/check.cpp` currently dispatches **only** the built-in RE2 rule engine
  (`keur-rules`); no external-tool or script scanner is invoked yet. So both A
  and B require first wiring a **scanner-dispatch step** into `check()` that reads
  the `[scanners.*]` inventory and runs enabled adapters. The seams exist:
  `adapters::Proc` already provides `run_argv` (shell-free) and `capture`
  (stdout), and `adapters::Git` can answer tracked-status. The project-local
  adapter (B) is then a thin runner over those seams + a JSONL→Finding parser —
  no new subsystem, but not a zero-line change either: the dispatch loop is the
  prerequisite that does not exist today.

**Neutral**

- Aligns keur further with the "orchestrate proven tools, gap-fill with rules"
  posture already stated in `config/keur.toml` and ADR-018.

## Not doing

- Reimplementing linters natively in the RE2 engine (violates ADR-001).
- A general runtime plugin system (violates ADR-002). The project-local adapter
  is a single fixed entry point, not a plugin loader.
