# Ground-truth roadmap — next steps

Captured 2026-09-17. Ideas for strengthening the training baseline BEFORE
running more learning. The theme of the session that produced these: a detector
is only as good as its benchmark, so validate the ground truth before
optimising against it.

## Where we are now

- Ground truth is built from proven security scanners via `eval/groundtruth.sh`
  adapters: **gitleaks, semgrep, trufflehog** (trufflehog added 2026-09-17).
- `eval/buildgt.sh` now **dedups/combines** findings that share
  `(repo, file, category)` into one record with a `tools` list and a `seen_by`
  count — clean statistics plus a confidence signal.
- `eval/measure.sh` gained two audit filters:
  - `KEUR_GT_TOOLS` — restrict the baseline to a proven tool subset
    (e.g. `gitleaks,semgrep`).
  - `KEUR_GT_CATEGORIES` — restrict BOTH the ground truth and keur's findings to
    a category scope (e.g. `secret,sast,cmdi,sqli`), so recall and precision are
    measured against the same scope.
- Clean measurement on this baseline: recall ~0.71, precision ~0.68, F1 ~0.69.
  Remaining gap is small and real: a handful of sqli / secret / sast misses.

## Idea 1 — more tool adapters + combination analysis before training

Add adapters for more sources (the design is source-agnostic — one
`run_<tool>()` function that normalises to the Finding JSON shape, exactly like
trufflehog):

- **GitHub Copilot** (code scanning / autofix findings)
- **CodeRabbit** (PR review findings)
- **Sonar** (SonarQube / SonarCloud issues)
- Candidates already installed locally but not yet wired: **trivy** (vuln/deps +
  IaC misconfig), **checkov** (IaC misconfig). Note: their IaC findings are
  largely NOT line-regex catchable, so they will widen the recall gap with cases
  the current line engine cannot reach — measure them under `KEUR_GT_CATEGORIES`
  scope, and treat them as motivation for the ADR-004 analysis-engine evolution
  rather than as line-rule targets.

Then, BEFORE training: analyse every tool COMBINATION as TP / FP / TN / FN.
Which tools agree? Which fire alone? Use the `seen_by` count to weight
confidence — a finding confirmed by multiple independent tools is more
trustworthy than a single-tool hit. Findings seen by only one tool deserve
scrutiny (they were the main source of noise in the 2026-09-17 session).

## Idea 2 — LLM-as-a-judge for ground-truth validation

Ask a local LLM (Ollama) to classify each finding: given a small code snippet
plus the tool's finding, is this a genuine issue (TP) or a false alarm (FP)?

- Purpose: **clean** the ground truth, not just grow it. This would have caught
  the `InvoiceController.java` case that `aws-security-agent` mislabelled as
  "sqli" but is a perfectly ordinary Spring REST controller.
- Hard boundary (keeps the design honest): the LLM may JUDGE the benchmark
  (help decide what is a true finding), but it must NEVER approve rules. Rule
  acceptance stays with the deterministic gate in `optimise.sh`. Otherwise the
  model grades its own homework.
- Fits the existing shape: judge output is just another signal per finding,
  alongside `tools` / `seen_by`.

## Suggested order

1. Wire more adapters (start with the ones already installed: trivy, checkov;
   then the API-based Copilot / CodeRabbit / Sonar).
2. Build the combination / agreement analysis (TP-FP-TN-FN per tool set).
3. Add the LLM-as-a-judge pass to prune mislabelled findings.
4. Only then resume training against the validated baseline.
