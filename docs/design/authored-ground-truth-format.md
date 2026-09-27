# Authored ground truth — common format & per-repo harvester

@id authored-ground-truth-format

> Companion to ADR-032. Where `eval/groundtruth.sh` builds ground truth from
> *scanner output* (a weak label — the tools have false positives), this defines
> a second, higher-trust ground truth built from the **vulnerability
> documentation each corpus repo ships itself** (`sinks.json`, challenge docs,
> per-vuln READMEs, inline markers). The repos are vulnerable-by-design; their
> own docs are authored labels, not guesses.
>
> Producer: `eval/gt-harvest.sh` (per-repo adapters → one common JSONL).
> Consumer: the benchmark (`measure.sh`) and the tool-accuracy report.

## Why a common format

The authored labels come at **different granularities** and from **different
document shapes** per repo (verified 2026-09-26):

| Repo | Authored source | Granularity | Has file/line? |
|------|-----------------|-------------|----------------|
| `gbleaney_python_security` | `sinks.json` (machine-readable) | function | no (function name) |
| `OWASP_crAPI` | `docs/challenges.md` + `challengeSolutions.md` | vuln-class (CWE) | no (endpoint hints) |
| `bridgecrewio_terragoat` | README + inline "vulnerable-by-design" | repo / file | partial |
| `we45_DVFaaS-...` | per-vuln `*/README.md` | directory (vuln folder) | folder → files |

A single flat "finding" record cannot hold that spread honestly. The common
format therefore makes **granularity and provenance explicit**, so a
function-level label from `sinks.json` and a class-level label from a challenge
doc coexist without pretending to a precision neither has.

## The record (one JSON object per line)

`eval/corpus/authored-groundtruth.jsonl` — one labeled case per line:

```json
{
  "schema": "keur.authored-gt/1",
  "repo": "gbleaney_python_security",
  "expect": "vulnerable",
  "category": "command-injection",
  "cwe": "CWE-94",
  "granularity": "function",
  "locator": { "function": "os.system" },
  "label_source": "sinks.json",
  "confidence": "high",
  "evidence": "authored machine-readable sink list (58 entries)",
  "reviewed": false
}
```

### Fields

| Field | Required | Meaning |
|-------|----------|---------|
| `schema` | yes | format version, `keur.authored-gt/1` |
| `repo` | yes | corpus repo dir name (matches `eval/corpus/repos/<repo>`) |
| `expect` | yes | `vulnerable` \| `safe` — the authored label (the ground truth) |
| `category` | yes | keur's coarse category (secret/command-injection/sqli/bola/…) |
| `cwe` | no | CWE id when the source names one |
| `granularity` | yes | `line` \| `file` \| `directory` \| `function` \| `class` \| `repo` — how precisely the label localises |
| `locator` | yes | object matching `granularity`: `{file,line}` \| `{file}` \| `{directory}` \| `{function}` \| `{cwe}`/`{challenge}` \| `{}` |
| `label_source` | yes | the authored doc the label came from (`sinks.json`, `docs/challengeSolutions.md`, `README.md#xxe`) — provenance |
| `confidence` | yes | `high` (machine-readable manifest) \| `medium` (structured prose) \| `low` (LLM-extracted, unreviewed) |
| `evidence` | yes | short human-readable why (the sentence/entry the label rests on) |
| `reviewed` | yes | `false` until a human adjudicates (mirrors `gt-suppressions.jsonl`) |

### Design rules (align with ADR-032)

- **The doc is the label, not a scanner.** `label_source` always points at an
  authored artifact in the repo, never at a tool.
- **Granularity is honest, not inflated.** A `sinks.json` entry is
  `granularity: function`; it does **not** claim a file/line it does not know.
  The benchmark decides how to match a function-level label (name presence) vs a
  line-level label (exact locus).
- **Confidence tracks provenance strength.** `high` = machine-readable manifest;
  `medium` = deterministically parsed structured prose (a `### Challenge N`
  heading); `low` = Ollama-extracted from free prose, always `reviewed:false`.
- **`reviewed:false` until adjudicated.** The LLM/parse proposes; a human
  confirms — same boundary as `gt-triage.sh`. The harvester never claims truth it
  has not earned.
- **Never edits the scanner-built `groundtruth.jsonl`.** This is a *separate*,
  parallel ground truth. The two are joined at measure time (see below).

## How the benchmark uses it

At measure time keur runs its engine over `repo`, then matches each finding
against the authored label at the label's own granularity:

```text
expect=vulnerable + keur fires at the locator  → TP
expect=vulnerable + keur silent                → FN
expect=safe       + keur fires                 → FP
expect=safe       + keur silent                → TN
```

- `granularity: line` → exact file+line match.
- `granularity: function` → keur fired on a call to that function anywhere in the
  repo (name-level match).
- `granularity: file`/`directory` → keur fired anywhere in that path.
- `granularity: repo`/`class` → coarse: "did keur find *this class* of issue in
  this repo at all" — a recall signal only, never used to charge an FP.

Crucially, **this is also how per-tool accuracy becomes measurable** (the
question ADR-032 could not answer): run semgrep/gitleaks/bandit over the same
repo, match their findings against these authored labels, and each tool gets a
real precision/recall. The "hundreds of hand-squashed FPs" become a measured
FP-rate instead of an anecdote.

## Per-repo adapters

The harvester is a dispatcher: one small adapter per label-source shape, all
emitting the common record. Adapters are added as repos are curated — not all 52
at once (the deliberate, slow path from ADR-032).

| Adapter | Handles | Method |
|---------|---------|--------|
| `sinks_json` | `gbleaney_python_security` (+ any repo with `sinks.json`) | parse JSON → function-level `vulnerable` labels; grep the repo for each sink to attach `{file,line}` where found (upgrades granularity function→line) |
| `challenge_md` | `OWASP_crAPI`, other OWASP challenge apps | parse `## <class>` / `### Challenge N` headings → class-level `vulnerable` labels + CWE map |
| `vuln_dir_readme` | `we45_DVFaaS-...` | each `*/README.md` under a vuln folder → directory-level label, category from folder name |
| `vulnerable_by_design` | `terragoat`/`cfngoat`/`cdkgoat`/goats | repo-level `vulnerable` label per IaC category (coarse recall signal) |
| `llm_prose` (fallback) | any repo with prose vuln docs and no structured source | Ollama extracts candidate `{category, cwe, file-hint}` → `confidence: low`, `reviewed:false` (curator role, ADR-032 §3) |

Unknown repos are skipped with a note, not guessed — the same "park, don't drop"
discipline as ADR-029.

## Non-goals

- Not a replacement for the scanner-built `groundtruth.jsonl` — a *complement*
  with higher trust and coarser localisation. The two are used together.
- Not automatic truth: `confidence:low`/`reviewed:false` entries are candidates
  until a human clears them.
- Not a marker-injector (yet): this harvests *existing* repo docs into an
  external label file; writing `KEUR-EXPECT` markers back into (author-owned)
  fixtures is a separate step for the hand-authored corpus only.
