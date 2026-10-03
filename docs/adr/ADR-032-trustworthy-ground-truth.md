# ADR-032: A trustworthy ground truth — tool output is a weak label, not truth

@id ADR-032

## Status

Proposed (options laid out; recommended path marked) · 2026-09-26 · Coverage: partial
· Relates to: ADR-023 (synthetic corpus), ADR-027 (measurement granularity),
ADR-029 (seed-injected learning), ADR-030 (revalidation), ADR-031 (independent validation),
`docs/design/keur-eval-convention.md`, `docs/design/keur-design-rationale.md`

## Context

keur trains and measures against a ground truth built from established tools —
semgrep, gitleaks, bandit, and cpm/aws-sec adapters (`eval/groundtruth*.sh`,
`eval/buildgt.sh`). The founding assumption is that "what the proven tools find"
is truth. **That assumption is wrong, and knowingly so:** semgrep and gitleaks
produce false positives as normal behaviour. gitleaks even ships allowlists
(paths + regexes + stopwords) precisely because it treats its own detections as
fallible. So a ground truth that equals "whatever the tools flagged" inherits
their false positives as *labels*.

The consequence, stated exactly (also in `architecture-evolution.md` §6.4):
training or measuring against that corpus optimises keur toward **reproducing the
other tools' mistakes**, and reports it as precision. keur then learns to answer

> "what does semgrep/gitleaks detect?"

when the question it must answer is

> "is this code, by keur's own rule, actually a defect?"

For a deterministic gate whose whole selling point is low-noise, high-signal
verdicts, that gap is disqualifying if left unaddressed.

Encouragingly, three pieces of the right answer already exist in the repo:

- **Markers** — `docs/design/keur-eval-convention.md` already defines
  `KEUR-EXPECT: <RULE>`, `KEUR-EXPECT: none`, and `KEUR-EXPECT-NOT: <RULE>` (the
  false-positive marker). This is keur's equivalent of Semgrep's `ruleid:` / `ok:`
  test annotations.
- **Weak labels** — `eval/buildgt.sh` already collapses findings across tools on
  `(repo, file, category)` and keeps a `seen_by` count: a finding confirmed by
  more tools is treated as more trustworthy. That is multi-tool weak labelling,
  not blind trust in one tool.
- **LLM triage as a fallible curator** — `eval/gt-triage.sh` already asks Ollama,
  per missed finding, "is this a real vulnerability or a false positive of the
  scanner?" and writes verdicts to `eval/corpus/gt-suppressions.jsonl` with
  `reviewed:false`; `measure.sh` drops only entries marked `false-positive`. It
  **never edits `groundtruth.jsonl`**. This is exactly "LLM as data curator, not
  security oracle."

So the decision is not to invent a new system — it is to **elevate "tool output
is a weak label, adjudicated into ground truth" from three partial scripts into
the corpus's governing model**, and to decide how far Ollama is trusted in that
loop.

## Current state (verified 2026-09-26) — markers are NOT used in the corpus

A direct inspection settles the "are markers standardised, or per-repo different?"
question honestly:

- **The harvested corpus does not use markers at all.** `eval/corpus/repos/`
  holds **52 third-party vulnerable-app repos** (juice-shop, DVWA, WebGoat,
  terragoat, NodeGoat, crAPI, …). A `grep` for `KEUR-EXPECT`/`KEUR-EXPECT-NOT`
  across them returns **0**; a grep for Semgrep's `ruleid:`/`ok:` also returns
  **0**. These are upstream repos the author did not write and cannot annotate —
  so there is no per-case ground-truth marker in the corpus, standardised or
  otherwise.
- **Ground truth for those repos is generated purely from tool output.**
  `eval/groundtruth.sh` runs the proven scanners and writes
  `{source, tool, rule, category, file, line, repo}` per finding — the union of
  what the tools flagged, keyed on `(repo, file, category)`. There is **no marker
  in the loop**: the label *is* the tool verdict. That is exactly the flawed
  assumption this ADR exists to fix.
- **The `KEUR-EXPECT` marker only lives on the hand-authored side.** It is defined
  in `keur-eval-convention.md` and consumed by the eval scripts
  (`keureval.sh`, `gen-cases.sh`, `build-index.sh`, `selfcheck-gate.sh`), and the
  hand-authored fixtures live under `tests/fixtures/`. So keur already has *two
  disjoint corpora with two different label models*: a small marker-labelled
  hand-authored set, and a large tool-labelled harvested set.

So: **can we generate ground truth "on markers"? Not from today's corpus — it has
none.** The marker convention exists and is sound, but it is applied only to the
small authored set, not to the 52 harvested repos that dominate the numbers. That
split is the concrete form of the correctness-vs-mimicry gap: the marker set
measures correctness; the harvested set measures mimicry of the tools.

## The overlooked label source: the repos document their own vulnerabilities

The harvested repos are **vulnerable-by-design training apps**, and such apps
almost always ship documentation of the exact defects they planted — that is
their purpose. The pipeline ignores this entirely and trusts only scanner output.
Verified examples already in the corpus:

- `OWASP_crAPI/docs/challengeSolutions.md` + `challenges.md` — every planted vuln
  documented by CWE class with a solution walkthrough.
- `gbleaney_python_security/sinks.json` — a **machine-readable** list of the
  vulnerable functions (`vulnerable_function`, `category_name`, sometimes a CodeQL
  source URL). Authored ground truth, JSON, ready to consume.
- `we45_DVFaaS-.../{xxe,injection,iam_privilege_escalation}/README.md` — per-vuln
  READMEs; DVWA, WebGoat, mutillidae, AndroGoat all document their challenges.

This is the answer to "why build/download a crappy repo to train on but ignore
the instructions it ships for finding the crappy shit?" — those instructions are
**authored labels**, higher-trust than a scanner guess, and free. The corpus has
them; the ground-truth builder does not read them.

**Decision addition:** add a third ground-truth source alongside tool output and
hand-authored markers — a **repo-manifest harvester** that reads each vulnerable
app's own vuln documentation (`sinks.json`, `challengeSolutions.md`, per-vuln
READMEs) into labelled cases. Where a repo publishes a machine-readable manifest
(`sinks.json`), it maps directly; where it publishes prose (challenge solutions),
Ollama does a first-pass extraction (curator role, §3) → human adjudication →
`KEUR-EXPECT` markers on the referenced files. This turns the harvested corpus
from tool-labelled (mimicry) into author-labelled (correctness) using labels the
repos already ship.

## Tool accuracy — measured against what, exactly?

"What is semgrep's / gitleaks's accuracy?" cannot be answered from the current
setup, and that is itself the finding: **there is no independent truth to measure
them against, because the tools *are* the truth in today's pipeline.** Measured
facts (2026-09-26):

- The ground truth is **9,614 findings** and is *entirely* tool output, split:
  bandit 22.9%, grype 18.8%, semgrep 15.8%, trivy 14.9%, checkov 13.9%, tfsec
  6.3%, njsscan 2.9%, gitleaks 2.4%, trufflehog 2.0%.
- `gt-suppressions.jsonl` (the triaged false-positive overlay) is **empty** — so
  *zero* of those 9,614 have been adjudicated as FP yet. Every one currently
  counts as "truth".
- The author reports having squashed **hundreds of false positives** by hand —
  direct lived evidence that a meaningful fraction of that 9,614 are wrong, and
  that the squashing happened in the rules/keur side, not recorded against the GT.

So the honest accuracy statement is: **unknown, and unknowable until an
independent label exists.** You cannot compute semgrep's precision by asking
semgrep. You *can* compute it once the repo-manifest labels (above) and the
hand-authored markers give an author-independent truth: then each tool's
`detected` vs the label yields a real per-tool precision/recall — and the
"hundreds of FPs" become a measured FP-rate instead of an anecdote. That
per-tool accuracy table is a direct output of adopting §2 (tools as annotators)
+ the manifest harvester, and it is the number that tells you *which* tools to
trust *how much* in corroboration (ADR-031, T-203).

## How other tools handle this (prior art)

- **Semgrep** annotates test code inline: `// ruleid: RULE` (must match),
  `// ok: RULE` (must not match), plus `// todoruleid:` / `// todook:` for known
  limitations. The label lives *in the source*, next to the code, authored by a
  human. keur's `KEUR-EXPECT` / `KEUR-EXPECT-NOT` is the same idea.
- **gitleaks** maintains explicit allowlists (paths, regexes, stopwords) — proof
  that a mature scanner does not treat its own detections as absolute truth.
- **SARIF** standardises the *result* layer (`ruleId`, `kind`, `level`,
  `locations`, `fingerprints`, `baselineState`) — but it is a scanner-*output*
  format, not a corpus-*labelling* format. Do not overload it as labels.

**There is no universal TP/FP/TN/FN source-code marker standard.** The closest
de-facto standard is Semgrep's `ruleid/ok` inline convention. So keur uses its own
marker (already defined) for labels, and SARIF only for exporting *results*:

```text
keur testcase (KEUR-EXPECT marker)  ← the label / ground truth
        ↓
keur engine
        ↓
result
        ↓
SARIF                               ← the result layer, for tooling/CI
```

## Decision

**Label the code, not the scanner output.** A test case declares its own truth via
an in-source marker; the scanners (keur, semgrep, gitleaks, …) are *annotators*
whose agreement is evidence, never the label. The confusion matrix
(TP/FP/TN/FN) is *derived* when the engine runs against the labelled corpus — it
is never stored as a fixed category, because the same case is a TP or an FN
depending on the engine's current behaviour.

### 1. Corpus shape — `expect`, not `TP/FP/TN/FN`

Model cases by *expected truth*, per rule family, positive vs negative:

```text
tests/
  hardcoded-secret/
    positive/     # expect: violation
      obvious.ts
      concatenated.ts
      multiline.ts
    negative/     # expect: clean
      documentation.ts
      example.ts
      env-var.ts
      fake-secret.ts
```

Each case carries the existing marker (`keur-eval-convention.md`):

```text
// KEUR-EXPECT: HS-001            # positive: this rule must fire
// KEUR-EXPECT: none              # negative: nothing must fire
// KEUR-EXPECT-NOT: HS-001        # this rule must NOT fire here (a known FP shape)
```

The runner then derives, per rule × case:

```text
expected     actual       => classification
violation    violation    => TP
violation    clean        => FN
clean        violation    => FP
clean        clean        => TN
```

This is why the corpus stores `expect`, not `TP/FP/TN/FN`: the matrix is a
*function of* (ground truth × engine run), computed by the benchmark (ADR-027),
not a stored fact.

### 2. Tools are weak-label annotators, disagreement is the signal

For every case, record what each tool said alongside the human/authored label
(extending `buildgt.sh`'s `seen_by`):

```yaml
id: HS-001
rule: hardcoded-secret
expect: clean              # the GROUND TRUTH (authored, adjudicated)
tools:
  keur:     { detected: true }
  semgrep:  { detected: true }
  gitleaks: { detected: false }
```

Here keur+semgrep firing on an `expect: clean` case is an **FP of keur and
semgrep** — *because the label says clean*, not despite the tools. Tool
disagreement (semgrep says vuln, gitleaks says clean) feeds a **review queue**,
not a majority vote — majority vote just launders correlated tool errors into
"truth". Adjudicated disagreement is far more valuable than agreement.

### 3. Ollama's role — curator, never oracle (the key architectural line)

```text
GitHub / OWASP corpus
        ↓
candidate extraction (tool findings = weak labels)
        ↓
Ollama classification  → { classification, confidence, reason }   (small, simple step)
        ↓
high-confidence candidates
        ↓
human review (audit the low-confidence + disagreements)
        ↓
adjudicated ground truth (KEUR-EXPECT markers)
        ↓
KEUR benchmark → confusion matrix → deterministic rule
```

Ollama does **one narrow job**: given a candidate case, emit a structured judgement

```json
{ "classification": "safe", "confidence": "high",
  "reason": "value is explicitly an OWASP example, not a live credential" }
```

It **never** sets a rule's pass/fail and **never** edits `groundtruth.jsonl`
directly — exactly the boundary `gt-triage.sh` already enforces (verdicts land in
`gt-suppressions.jsonl` with `reviewed:false`, human-auditable). This keeps the
determinism guarantee (`keur-design-rationale.md`: "AI may add evidence; AI may
never be the referee") intact for the *corpus*, not only the gate.

### 4. Mutation-based case families (highest-leverage generator)

Rather than thousands of random GitHub snippets, take one real defect and mutate
it systematically into a labelled family — the cheapest way to get clean
positive *and* negative cases that differ by exactly the thing the rule must key
on:

```text
                  hardcoded-secret
        ┌────────────────┼────────────────┐
     violation          clean          ambiguous
     literal            env var        documentation
     concatenated       test fixture   commented-out
     template           fake/example   generated
```

```ts
const password = "secret123";              // KEUR-EXPECT: HS-001  (violation)
const password = "secret" + "123";         // KEUR-EXPECT: HS-001  (violation)
const password = process.env.PASSWORD;     // KEUR-EXPECT: none     (clean)
const examplePassword = "not-a-real-one";  // KEUR-EXPECT-NOT: HS-001 (clean, known-FP shape)
// password = "secret123";                 // KEUR-EXPECT: none     (comment, clean)
```

This is also where Ollama earns its keep as a *generator* of candidate variants
(then adjudicated), and it dovetails with ADR-023 (synthetic corpus) and
`eval/perturb.sh` (perturbation already exists).

### Minimal starting schema

Start as small as this (one file per case + a marker, or a manifest):

```yaml
id: HS-001
rule: hardcoded-secret
expect: violation          # or: clean
language: typescript
source: { type: handcrafted, reference: CWE-798 }
code: |
  const password = "secret123";
```

## Recommended path (small, deterministic steps — the keur way)

1. **Make `expect` the corpus contract.** Reorganise `eval/corpus/` toward
   `tests/<rule-family>/{positive,negative}/` with `KEUR-EXPECT` markers, and make
   the runner derive TP/FP/TN/FN (it already has the pieces).
2. **Record tool verdicts as weak labels, keep `seen_by`** (extend `buildgt.sh`) —
   but the marker, not the tool, is the label.
3. **Route disagreement + low-confidence to a review queue**, adjudicated by a
   human, assisted by Ollama's structured judgement (extend `gt-triage.sh` from
   FN-only to both directions). Ollama outputs `{classification, confidence,
   reason}`; humans clear the queue; nothing auto-edits ground truth.
4. **Grow with mutation families**, not random snippets, so every positive has its
   near-miss negative (ties into ADR-023 + `perturb.sh`).
5. **Export results as SARIF** for CI/tooling — results only, never labels.

Each step is independently shippable and reuses existing scripts; none requires a
new subsystem.

## Consequences

**Positive**
- The corpus measures *correctness* (does keur match the adjudicated label), not
  *mimicry* (does keur match semgrep). This is the difference between "consistent"
  and "correct" from ADR-030/031, finally closed at the source.
- Tool false positives stop silently becoming labels; they become review-queue
  items with an audit trail.
- Ollama is used where it is safe and strong (curation, candidate generation,
  first-pass triage) and nowhere it is dangerous (deciding truth or verdicts).
- Builds on machinery that already exists (`KEUR-EXPECT`, `buildgt.sh` weak
  labels, `gt-triage.sh` LLM triage), so it is adoption + formalisation, not
  invention.

**Negative / cost**
- Adjudication is human work. It is the deliberate, slow, decisive work keur chose
  over cpm's bulk generation — accept it; it is the point.
- Mutation families and marker discipline are ongoing curation effort.
- A reorganised corpus layout touches `measure.sh`/`buildgt.sh` and the history
  format; do it behind the existing `.bak` discipline and revalidate (ADR-030).

## Alternatives considered

- **Keep tool output as ground truth (status quo):** rejected — it teaches keur to
  reproduce other tools' false positives and calls it precision.
- **Majority vote across tools as truth:** rejected — correlated tool errors
  survive a vote; disagreement must be *adjudicated*, not averaged.
- **Let Ollama label the corpus autonomously:** rejected — the LLM is fallible
  (`gt-triage.sh` says so explicitly); it curates and proposes, a human and the
  benchmark decide.
- **Use SARIF as the label format:** rejected — SARIF is a result/output format;
  labels live in the source marker (`KEUR-EXPECT`), SARIF exports the run.
- **Invent a new TP/FP/TN/FN marker standard:** unnecessary — there is no
  universal one; Semgrep's `ruleid/ok` is the de-facto convention and keur already
  mirrors it. Store `expect`; derive the matrix.

## Open questions

- The exact review-queue tool (a simple CLI over `gt-suppressions.jsonl`, or the
  manager TUI?) and how a human marks a case `reviewed:true`.
- Whether mutation families are hand-written, Ollama-generated-then-adjudicated,
  or both — and how `perturb.sh` (ADR-023) feeds them.
- License-clean sources for the external, author-independent slice (ADR-031 §B):
  OWASP examples and CWE demonstrative examples are candidates; confirm licences
  before vendoring.
- How the `expect`-based corpus and the existing `(repo,file,category)` groundtruth
  coexist during migration (dual-read in `measure.sh`, then cut over).
