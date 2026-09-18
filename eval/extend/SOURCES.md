# Knowledge sources — adapter specs & boundaries

The keur-eval extender turns security *knowledge* into fresh **fictional** test
cases. Knowledge comes from **knowledge adapters**, each emitting the same
normalised concept shape (see `README.md`). This document specifies the
legitimate sources we may add, how each maps onto the concept schema, and the
sources we will **not** use.

The golden rule: **sources provide concepts; the generator writes fresh
fictional code.** We never copy real code, real findings, or real secrets into
the corpus. That keeps it licence-clean and free of PII.

## Concept schema (recap)

```json
{"source":"...", "concept_id":"...", "title":"...", "domain":"...",
 "severity":"...", "cwe":["CWE-..."], "langs":["..."], "hint":"..."}
```

## Allowed sources

### 1. cpm rules — `knowledge-cpm.sh` ✅ implemented
- **What:** cpm's own `category: security` rule corpus (in-house).
- **Licence:** ours. No restriction.
- **Mapping:** `id→concept_id`, `title`, first `message→hint`, `CWE-\d+→cwe`,
  `target.extensions→langs`.

### 2. CWE / MITRE — implemented (`knowledge-cwe.sh`)
- **What:** the CWE weakness catalog (e.g. CWE-89 SQL Injection). MITRE
  publishes it as a downloadable XML/CSV dump.
- **Licence:** CWE is free to use with attribution (MITRE terms). Cite the CWE
  ID + version; do not imply MITRE endorsement.
- **Access:** download the official CWE list archive (no scraping, no auth).
- **Mapping:** `CWE-<id>→concept_id` and `cwe`, CWE Name`→title`, weakness
  description → `hint`, `domain` from a CWE→domain table; `langs` left broad.
- **Value:** authoritative taxonomy; anchors our `domain`/`cwe` fields.

### 3. OWASP — implemented (`knowledge-owasp.sh`)
- **What:** OWASP Top 10 categories and Cheat Sheet Series topics.
- **Licence:** OWASP content is Creative Commons (CC-BY-SA). Attribute; keep
  our derived cases as our own fictional code (CC covers the prose, not code we
  write ourselves).
- **Access:** the OWASP repos are on GitHub with a clear licence — clone/read
  the markdown, do not scrape the website.
- **Mapping:** Top-10 item / cheat-sheet title → `title`, its CWE refs → `cwe`,
  `domain` from the item, prose summary → `hint`.

### 4. Semgrep open registry — implemented (`knowledge-semgrep.sh`)
- **What:** Semgrep's open-source rule registry (rule *metadata*: id, message,
  CWE/OWASP tags, languages).
- **Licence:** the community registry is open-source (LGPL/Commons Clause varies
  per pack — check per pack). Use **metadata only** (title/CWE/langs), never
  copy a rule's `pattern` or example verbatim.
- **Access:** the registry is a public git repo; read the YAML metadata.
- **Mapping:** rule `id→concept_id`, `message→hint`, `metadata.cwe→cwe`,
  `languages→langs`.

### 5. GitHub Security Advisories (GHSA) — implemented (`knowledge-ghsa.sh`)
- **What:** the GitHub Advisory Database (CVE/GHSA entries: summary, CWE,
  affected ecosystem).
- **Licence:** the advisory database is CC-BY-4.0 and there is an official
  **GraphQL/REST API** — no scraping.
- **Access:** authenticated GitHub API with a token; respect rate limits.
- **Mapping:** `ghsaId→concept_id`, `summary→title`+`hint`, `cwes→cwe`,
  ecosystem→`langs`, `domain` from the CWE.
- **Note:** use the *class of weakness* as inspiration, never the vulnerable
  code from the referenced project.

## NOT allowed (explicit boundaries)

Do **not** build adapters for, or feed the generator from:

- **Sonar / SonarQube / SonarCloud instances** — scraping a dashboard or an
  account (yours or anyone's) violates ToS and mixes in others' code/findings.
- **CodeRabbit or any code-review SaaS** — same reason; their findings and the
  code they review are not ours to harvest.
- **Copying real code** from repositories, pull/merge requests, "security fix"
  commits, books, or paywalled articles into the corpus. The corpus is
  **fictional-only** (keur-eval README, "Data rules"). Real fix commits may be
  read by a human for *understanding*, but the case that lands is fresh
  fictional code the generator wrote.
- **Anything whose licence or Terms of Service forbid reuse**, or that would
  bring real secrets / PII into the repo.

If a prospective source does not have (a) public access without circumventing
auth, and (b) a licence that permits reuse of the *metadata* we extract, it does
not get an adapter.

## Adding an adapter (checklist)

1. Confirm the source is on the allowed list (public + licence-clean).
2. Emit the concept schema to `knowledge/<source>.jsonl` — metadata only.
3. Add a row to the adapter table in `README.md`.
4. The generator, gate, and promotion flow are unchanged — they are
   source-agnostic by design.
