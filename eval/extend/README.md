# eval/extend — the keur-eval extender

A pipeline that grows the `keur-eval` corpus with fresh, **fictional** security
cases and a trace of *why* each is (or is not) a finding. It never scrapes
proprietary dashboards and never copies real code; sources provide *concepts*,
and a local LLM writes new fictional code from them.

```text
knowledge adapters  →  knowledge cache (JSONL)  →  Ollama generator (draft + trace)
     │                                                    │
     │                                          keur self-check gate (does the
     │                                          expected rule fire? negative quiet?)
     │                                                    │
     └── same normalised concept shape          drafts that pass → review-queue/
         from every source                               │
                                          human + Kiro validate & promote  ← OUTSIDE the loop
```

The loop stops at the review queue. Promotion into `positive/ | negative/ |
regression/` happens by a human, never auto-committed.

## Knowledge adapter contract

Each adapter is a small script that emits **one JSON object per line** to
`eval/extend/knowledge/<source>.jsonl`, all with the same normalised shape:

```json
{
  "source":     "cpm-rules",
  "concept_id": "cpm:BE-SEC-015",
  "title":      "Server-side request forgery (SSRF)",
  "domain":     "ssrf",
  "severity":   "error",
  "cwe":        ["CWE-918"],
  "langs":      ["ts","js","py"],
  "hint":       "User input in HTTP request URL — SSRF risk."
}
```

The generator downstream consumes this shape and does not care which adapter
produced it. To add a source, write a new adapter that emits the same shape.

### Adapters

| Adapter | Source | Status |
|---------|--------|--------|
| `knowledge-cpm.sh` | cpm's `category: security` rule corpus (in-house, licence-clean) | ✅ implemented |
| `knowledge-cwe.sh` | the public CWE catalog — CWE-699 Software Development view (MITRE) | ✅ implemented |
| `knowledge-owasp.sh` | OWASP Top 10 (2021), CC-BY-SA | ✅ implemented |
| `knowledge-ghsa.sh` | GitHub Security Advisory Database via the official REST API (CC-BY-4.0) | ✅ implemented |
| `knowledge-semgrep.sh` | Semgrep's open rule registry — metadata only, from a local clone | ✅ implemented |

See "Legitimate sources & boundaries" below before adding one.

## Usage

```bash
# 1. Build the knowledge cache from one or more sources (all emit the same shape).
eval/extend/knowledge-cpm.sh                       # cpm's 273 security rules
eval/extend/knowledge-cwe.sh                       # 399 CWE weaknesses (downloads from MITRE)
eval/extend/knowledge-owasp.sh                     # OWASP Top 10 (2021)
eval/extend/knowledge-ghsa.sh --per-page 30        # GitHub advisories (set GITHUB_TOKEN for more)
eval/extend/knowledge-semgrep.sh --registry ~/semgrep-rules   # metadata only, from a local clone
eval/extend/knowledge-cpm.sh --stats               # summarise any cache

# 2. Draft fictional cases from concepts (Ollama; --mock to test without a model).
#    KNOWLEDGE selects which cache to draw from (default cpm.jsonl).
eval/extend/gen-from-knowledge.sh --domain secrets --limit 5
KNOWLEDGE=eval/extend/knowledge/cwe.jsonl eval/extend/gen-from-knowledge.sh --mock --domain cmdi --limit 2

# 3. Self-check gate: run keur over every draft (positive must fire its rule,
#    negative must stay quiet). Writes .gate-pass.jsonl / .gate-fail.jsonl.
eval/extend/selfcheck-gate.sh

# 4. Human + Kiro review, OUTSIDE the loop (no auto-commit).
eval/extend/promote.sh --list
eval/extend/promote.sh --show    review-queue/secrets/<draft>.py
eval/extend/promote.sh --promote review-queue/secrets/<draft>.py
# then, by hand:  cd keur-eval && git diff && git commit
```

Steps 1–3 are the loop (drafts + automatic gate). Step 4 is the human gate:
`--promote` moves a reviewed draft into the corpus and rebuilds `index.json`,
then stops — it never commits.

## Legitimate sources & boundaries

**Allowed** (public, structured, licence-clean, and used as *concepts* only):
CWE/MITRE, OWASP materials, the Semgrep open registry, the GitHub Security
Advisory API, public standards, and our own cpm/keur rule corpora.

**Not allowed** (do not build adapters for these):

- Scraping Sonar, CodeRabbit, or any third-party SaaS dashboard / account.
- Copying real code from repositories, merge requests, books, or articles into
  the corpus. The corpus is **fictional-only** (see keur-eval README).
- Anything whose licence or ToS forbids reuse.

Sources inspire; the generator writes fresh fictional code. That keeps the
corpus licence-clean and free of real secrets/PII.

See **[SOURCES.md](SOURCES.md)** for concrete adapter specs (CWE/MITRE, OWASP,
Semgrep registry, GitHub advisories), how each maps onto the concept schema, and
the full list of disallowed sources.
