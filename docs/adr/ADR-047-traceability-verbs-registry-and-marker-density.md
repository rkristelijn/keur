# ADR-047 — Data-driven traceability verbs, mandatory file headers, and marker density

@id ADR-047

**Status**: Proposed · **Date**: 2026-10-07 · **Coverage**: code · **Extends**: ADR-010 (the four-verb traceability model — this adds a verb registry, two verbs, a header rule, and a density axis) · **Relates to**: ADR-002 (single binary — why the registry is data, not a tool), ADR-016 & ADR-017 (doc-coverage report + surfacing), ADR-036 (local-LLM explainers — the summary generator reuses that path), DOC-013 (the existing undocumented-header rule the floor supersedes), `@external cpm:check-comment-ratio` (the 20% comment-ratio metric this adopts for parity)

## Context

ADR-010 gave keur four grep-able verbs (`@implements`, `@proves`, `@see`,
`@external`) measured by `eval/doc-coverage.sh` as set arithmetic. It works, but
three limits surfaced:

1. **The verbs are hardcoded in the meter.** Each verb is its own `grep` block in
   `doc-coverage.sh`. Adding a verb means editing shell. There is no single list
   that answers "which `@`-tags does keur support?" — the convention lives in
   prose (`traceability.md`) and the mechanics are scattered in a script.
2. **Only two deliverable types are measured: code and test.** A mature package
   has more deliverables (the V-model: requirements, design, docs, demos, not just
   code+test). keur can show that a concept is *built* and *tested*, but not that
   it is *explained* (a human-readable guide) or *demonstrated* (a visual demo).
3. **Coverage is per-concept, never per-file.** A source file can carry zero
   traceability markers and the meter says nothing — the blind spots (files
   coupled to no decision at all) are invisible.

The verbs keur already uses are not arbitrary: `@implements` and `@see` are
**JSDoc/Doxygen-standard**. `@proves` is keur's own, in the same grammatical shape
(verb, third-person, "this artefact *does* X to a concept"). That mix — adopt the
standard, add our own sauce — is the pattern to formalise, not abandon.

## Decision

### 1. The verb set is a registry (data, not code), read by a C++ subcommand

A single source of truth lists every supported verb and its behaviour:
`docs/design/traceability-verbs.tsv`. The meter reads it; adding a verb is a new
row, never a code edit. This keeps the single-binary rule (ADR-002): the registry
is data the meter consumes, not an external tool.

**The meter is a C++ subcommand (`keur trace`), not a shell script.** Today it is
a prototype in `eval/doc-coverage.sh` (set arithmetic in shell). That script
proved the model but it conflicts with "as much in C++ as possible" and with the
single-binary rule — a shell meter does not travel in the binary and an adopter
must copy it separately. So the meter is folded into the binary as `keur trace`:
it reads the TSV registry, harvests the tags (the rule engine already reads tags),
and reports every dimension (code/test/doc/engine + demo/guide + density), with
`--strict` as the gate. Build it ONCE in C++; `doc-coverage.sh` is the prototype
it replaces, kept only until `keur trace` reaches parity, then removed. No new
deliverable type is ever a shell edit — it is a registry row the C++ meter reads.

Columns (tab-separated, POSIX-greppable, macOS + Linux):

```tsv
# verb        source          kind   validated  deliverable  coverage-dim
id            keur            decl   -          -            -
implements    jsdoc,doxygen   ref    yes        code         concept->code
proves        keur            ref    yes        test         concept->test
shows         keur            ref    yes        demo         concept->demo
explains      keur            ref    yes        guide        concept->guide
see           jsdoc,doxygen   ref    no         related      -
external      keur            ref    no         cross-repo   -
```

- **source** — where the verb comes from (`jsdoc,doxygen` = standard; `keur` =
  our sauce). Documentation, not behaviour, but it makes "standard vs custom"
  explicit and lets a reader trust the standard ones.
- **kind** — `decl` (declares an `@id`) or `ref` (points at one).
- **validated** — must the target resolve to a declared `@id`? (`@see` is `no`:
  it may be a path/url/free name, per ADR-010.)
- **deliverable** — which deliverable type this verb represents.
- **coverage-dim** — the axis it contributes to in the report, or `-` if it is
  recorded but not a measured dimension.

### 2. Two new verbs, same shape as the standard ones

- **`@shows <id>`** — this artefact *shows* the concept visually: a demo
  recording (vhs `.tape` → GIF), a screenshot flow. Closes concept → demo. The
  verb is third-person like `@proves`/`@implements`; it points at a stable concept
  ID (rule id / engine-id / ADR), never a file. (We chose `@shows` over `@demo`
  because the set is verbs, not nouns — `@demo` would break the grammar.)
- **`@explains <id>`** — this artefact *explains* the concept in prose for a
  human: a how-to, a one-pager, a guide section. Closes concept → guide.

`@shows` and `@explains` collide with **no** existing standard (no doc-tool uses
them), so adopting them is safe. Together with `@implements`/`@proves` they map
onto the V-model deliverables: requirement/design (`@explains`), code
(`@implements`), test (`@proves`), plus demo (`@shows`) for enablement.

### 3. Every file carries a header block — and the header carries a marker

Adopt the **Doxygen file documentation block** (the `@file` convention) as the
mandatory per-file header. It is the standard name for "the required prologue
comment at the top of a file", and keur is C++ so Doxygen is the native norm.
keur's sauce: the header must also contain a one-line **summary** (`@brief`) and
**at least one traceability verb**.

```cpp
/**
 * @file rule/composite.cpp
 * @brief Evaluates composite rules against the fact set.
 * @implements ADR-009
 */
```

This merges two wishes into one check: "every file has >=1 marker" (the file
floor) and "every file has a documented header" become the same rule. A file with
no header, or a header with no verb, is an **orphan file** — the strongest, lowest
-noise density signal.

### 4. Density gates — two independent capabilities, each its own knob

Two different questions, so two separate rules — not one combined check. Each is
a normal keur rule, so each has its OWN `severity`/`action` and threshold and can
be steered independently (one `block`, the other `warning`, in any mix). This
reuses the existing gate mechanism (ADR-010: "the gate belongs in the engine");
no new `enforce` config surface is needed — the rule's `severity`/`action` IS the
ratchet.

| Capability | Engine | Measures | Threshold | Mitigates | Standard |
|------------|--------|----------|-----------|-----------|----------|
| **comment-ratio** | `comment-ratio` (new) | comment lines / total | `min_ratio` | unreadable, undocumented code | cpm parity (its `check-comment-ratio.sh`, 20%) |
| **marker-density** | `min-marker-density` (new) + the `absence` floor | traceability verbs / lines, and file-floor >=1 | `min_markers_per`, `file_floor` | files coupled to no concept at all | keur's own |

Both build on the scanner split the `max-lines` engine already does (it strips
comments to count net lines, so comment-vs-code separation exists); the two new
engines add a ratio/threshold over that same split — a small, bounded addition,
not a new subsystem.

```yaml
# comment-ratio — the familiar cpm metric (every comment counts)
id: KEUR-DOC-010
engine: comment-ratio
min_ratio: 20
severity: warning        # knob 1 — own threshold, own block/warn
target: { extensions: .c .cpp .cc .h .hpp }

# marker-density — the traceability layer (only @-verbs count)
id: KEUR-DOC-011
engine: min-marker-density
min_markers_per: 80      # >=1 marker per 80 lines
file_floor: true         # and every file >=1 marker/header
severity: warning        # knob 2 — independent of knob 1
target: { extensions: .c .cpp .cc .h .hpp }
```

Why TWO, not one: they measure different things (readability vs traceability) and
ratchet on different timelines. A team wanting only cpm parity enables
`comment-ratio` alone; a team wanting pure traceability enables `marker-density`
alone. Keeping them separate also keeps the mitigation honest — `marker-density`
counts only meaningful `@`-verbs (no cargo-cult from filler comments), while
`comment-ratio` is the blunter, well-known metric developers already recognise.

Ratchet, not big-bang: both default to `severity: warning` (reported, non-gating,
ADR-010's lesson that a 100%-or-red gate gets disabled), moved to a blocking
severity per-rule once the baseline is clean. The **file-floor** half of
`marker-density` is available with no new engine — a plain `absence` rule flagging
a file that carries no `@`-verb at all (a stricter, meaningful `DOC-013`).

Both capabilities report per file-type (code/doc/test have different natural
densities) and log a repo aggregate to `eval/metrics.csv` (ADR-017), so the trend
is visible and you can prove you drove it up.

### 5. Summaries may be LLM-proposed, never LLM-claimed

For files missing a `@brief`, a local model (the ADR-036 explainer path, the
`eval/Modelfile`) may *propose* a one-line summary. It is bounded, hard:

- The LLM proposes **only the `@brief` prose**. It never writes `@implements` /
  `@proves` / `@shows` — choosing the concept a file realises is a semantic
  judgement, and ADR-010's rule holds: evidence proposes, a human decides.
- Output is a diff a human accepts, exactly like `eval/xref-harvest.sh`.

## Why not the alternatives

- **Keep verbs hardcoded in the meter**: every new deliverable type is a shell
  edit and there is no list to point adopters at. The registry *is* the answer to
  "what do we support?".
- **A heavier doc tool (real Doxygen run, OpenFastTrace)**: rejected by ADR-010 /
  ADR-002 for the same reason — JVM/− toolchain in the hook path. We take the
  Doxygen *tag convention*, not its binary.
- **Per-function / per-section marker rule**: highest cargo-cult risk — markers
  become ritual. The file floor is the honest floor; per-function is out of scope.
- **One combined "comment coverage" check**: readability and traceability are
  different questions that ratchet on different timelines; one check forces one
  threshold and one knob. Two rules, each its own `severity`, steer independently.
- **Raw comment-ratio only (cpm's cloc model, every comment counts)**: easy to
  game — filler comments satisfy the percentage. Kept as the `comment-ratio`
  capability for parity/recognisability, but paired with `marker-density` which
  counts only meaningful `@`-verbs so the honest signal is not gameable.
- **A `[traceability.density] enforce` config block**: a second gate surface to
  learn. Rejected for the rule's own `severity`/`action` — one ratchet mechanism,
  the one adopters already know (ADR-010: the gate belongs in the engine).
- **`@demo` (noun)**: breaks the verb grammar of the set. `@shows` is the verb.

## Scope & limits

- **In scope**: the TSV registry; `@shows` + `@explains`; the mandatory
  header-with-marker rule; the `absence` file-floor rule (no new engine); the two
  density engines (`comment-ratio`, `min-marker-density`) each with its own
  `severity`/`action`; the LLM `@brief`-propose path (behind human accept).
- **Out of scope (follow-ups)**: shipping the two engines as blocking (they land
  as `warning` first and ratchet per-rule); the clickable-navigation `--fix` from
  ADR-010's TODO.
- **Explicit only**, inherited from ADR-010: nothing is inferred; a marker is a
  tag a human wrote (or accepted from a proposal).

## Acceptable gaps (what the gate tolerates)

Same split as ADR-010 — errors always block, incompleteness is tolerated while
visible.

| Gap | Verdict | Why |
|-----|---------|-----|
| File with zero markers, floor rule at a blocking severity | **BLOCK** | the floor is the contract once ratcheted up |
| File with zero markers, floor rule at `warning` | **WARN** | reported while the baseline is cleaned, not yet gated |
| `@shows x` / `@explains x` with no such `@id` | **BLOCK** | dead link, same as any ref (ADR-010) |
| Concept with no `@shows` yet | **WARN** | not every concept needs a demo; must stay visible |
| Concept with no `@explains` yet | **WARN** | a guide is desirable, not mandatory per concept |
| `comment-ratio` / `min-marker-density` below threshold | **follows the rule's `severity`** | each rule ratchets independently (warn → block) |

A concept opts out of a dimension honestly with a header value, mirroring
ADR-010's `Coverage: none`: `Demo: none` / `Guide: none` on the ADR/rule means
"deliberately no demo/guide", so the meter shows no false gap.

## Consequences

- **Positive**: one list answers "which `@`-tags does keur support?", mixing
  standard (JSDoc/Doxygen) and keur verbs, extensible by a TSV row — custom
  traceability without touching the meter.
- **Positive**: coverage grows from code+test to the mature-package set
  (explained, demonstrated), aligned to the V-model deliverables.
- **Positive**: the file-header floor turns "uncoupled file" from invisible into
  a measured, ratchetable number — and the header doubles as Doxygen docs.
- **Negative / caution**: density can invite ritual markers; mitigated by keeping
  the floor as the real gate and density as reporting until deliberately ratcheted.
- **Negative / caution**: LLM summaries can be bland or wrong; mitigated by
  human-accept and by forbidding the LLM from writing the link verbs.
- **Portable**: an adopting team gets the registry, the two verbs, the header
  rule, and the density knob with the same `keur trace` subcommand — no per-repo
  script to copy, it travels in the binary.
