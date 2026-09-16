# KEUR — proven-tech baseline (what to encapsulate, not reinvent)

> Research: 2026-09-16. Question — do we have all the ready-made scanners, incl. a
> PII scanner? Answer: **yes, and more than expected.** KEUR should lean on proven
> tools first; KEUR's own `.rule` engine is a *fallback / gap-filler*, not the base.

## Installed on this machine (proven, ready to encapsulate)

All present on PATH (via brew), all deterministic, all local:

| Tool | Domain | Role in KEUR | Base conf |
|------|--------|--------------|-----------|
| **gitleaks** | secrets (regex + entropy) | secrets gate | 0.99 |
| **trufflehog** | secrets (+ live verification!) | secrets gate (verify) | 0.99 |
| **trivy** | deps / SBOM / IaC / containers | dependency + IaC gate | 0.95 |
| **grype** | deps / CVE (from SBOM) | dependency gate (2nd source) | 0.90 |
| **syft** | SBOM generation | feeds grype/trivy | — |
| **osv-scanner** | deps / OSV database | dependency gate (3rd source) | 0.90 |
| **semgrep** | SAST (code patterns) | code gate | 0.80 |
| **checkov** | IaC (Terraform/CFN/K8s) | IaC gate | 0.85 |
| **tfsec** | Terraform security | IaC gate (2nd source) | 0.85 |

That's **9 proven scanners already installed** — vs the 3 I first stubbed in
keur.toml. The secrets layer alone has 2 independent tools (gitleaks + trufflehog),
deps has 3 (trivy + grype + osv-scanner), IaC has 2 (checkov + tfsec). This is
exactly the **corroboration** model: independent tools agreeing → higher confidence.

## The PII scanner already exists (this was the question)

`~/tooling/src/tools/ticket-audit/pii-sensitive-scan.sh` — deterministic, offline,
**production-proven**. Do NOT rewrite this in KEUR's rule engine. Key properties:

- **Validated** structured identifiers, not dumb regex:
  - BSN → 11-proef (elfproef) validation
  - IBAN → mod-97 checksum validation
  - NL phone, email
- Reuses **cpm's shared secret regexes** (sk-, AKIA, ghp_, glpat-, private keys…).
- APS-specific SENSITIVE patterns (internal hostnames `HQOV-MAN01`, RFC1918 IPs,
  internal domains) — **deliberately kept private** ("must NOT live in public cpm").
- Output contract: one line `PII|SENSITIVE|CLEAN <detail>`, exit 0 always.
- Precedence: PII > SENSITIVE > CLEAN.

Optional richer PII: **Presidio** (Microsoft) is wired as an optional local venv in
`multi-scan-eval.sh` — entity types + score, no values. Encapsulate as an optional
higher-fidelity PII source.

## The evaluator loop ALSO already exists (as a prototype)

`~/tooling/src/tools/ticket-audit/multi-scan-eval.sh` is, in effect, KEUR's
self-improving evaluator running today:

- **Ollama (local) = ground-truth labeler** (CBI + PII booleans, cached by content hash).
- Runs ALL detectors per file, logs **redacted metadata only** (no values, no content).
- Measures **agreement/disagreement** between Ollama and each deterministic detector
  ("CBI agree=X% PII agree=Y%") → exactly the "noisy oracles vs corpus" comparison.
- Leak-safe, resumable (hash cache), time-boxed, disk-guarded, local-only.

This is the working seed of `docs/design/evaluator.md`. KEUR's eval harness should
**generalise this**, not start over: same redacted-JSONL discipline, same
agreement-metric, same safety controls (max-runtime, stop-file, size cap, cache).

## The orchestration pattern already exists (RFC + scan.sh)

`~/tooling/docs/proposals/rfc-consolidated-scan.md` + the `ai-review/scan.sh` +
`.config/scan.toml` already define KEUR's aggregation contract:

- **One JSON line per finding**: `{severity,file,line,check,message}` → this IS the
  normalised Finding interface (KEUR adds source/confidence/fingerprint/deterministic).
- `.config/scan.toml` with `blocking` / `disabled` / advisory-by-default → this IS
  KEUR's policy config, just renamed.
- Auto-discovery of `checks/**/*.sh`, JUnit XML output, summary table, exit-code gate.
- Severity → blocking mapping. GitLab picks up JUnit in the MR widget.

## What this means for KEUR (revised plan)

KEUR is **less "build detectors" and more "encapsulate + normalise + decide"**:

1. **Adapters over proven tools** (thin wrappers → normalised Finding):
   gitleaks, trufflehog, trivy(+grype+osv), semgrep, checkov(+tfsec), the existing
   `pii-sensitive-scan.sh`, optional Presidio.
2. **Evidence aggregator + corroboration**: when 2+ independent tools agree on a
   fingerprint, raise confidence (secrets: gitleaks+trufflehog; deps: trivy+grype+osv).
3. **KEUR's own `.rule` engine = gap-filler only** — for domain/APS-specific patterns
   that no proven tool covers (kept private, like the pii-scan's APS patterns). Never
   the primary detection layer.
4. **Reuse the redacted-eval harness** from `multi-scan-eval.sh` as the CPM-Eval bridge.
5. **Reuse the scan.sh JSON+JUnit contract** as KEUR's output layer.

## Licensing note

Encapsulating (shelling out to) these tools is fine — they run as external
processes. We ship adapters + config, not their code. `pii-sensitive-scan.sh` and
the APS patterns are ours (private tooling). Sonar rules: study only, don't clone
(SSALv1 for post-2024-11 analyzers).

## Concrete next step

Update `config/keur.toml` scanners to reflect the real 9-tool set with corroboration
groups, and build the first adapter (`pii-sensitive-scan.sh` is the easiest —
it already emits a clean verdict contract). Then the secrets corroboration pair
(gitleaks + trufflehog).
