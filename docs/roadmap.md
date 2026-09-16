# KEUR — Roadmap

> Milestones + tickets. Order reflects the goal: **install must work first**, then
> **opt-in to install proven/native tools**, then **train to reduce dependency on
> them**, then **absorb cpm's full rule corpus**. Status: 🔴 todo · 🟡 partial · ✅ done.

## Rule feature-coverage analysis (answers "how is rule coverage?")

cpm has **1004 rule files → 2008 engine directives** (a rule can list several).
Engine spread across the whole corpus:

| Engine | Count | What it does | KEUR support needed |
|--------|------:|--------------|---------------------|
| `pattern` | 1712 | regex match (with target filter) | **M4a — first** |
| `presence` | 136 | a required thing must be present | M4b |
| `absence` | 134 | a forbidden thing must be absent | M4b |
| `file-absence` | 10 | a file must not exist | M4c |
| `extract-duplicates` | 6 | extract + dedupe (e.g. dup routes) | M4c |
| `block-absence` | 6 | a block/section must be present | M4c |
| `file-presence` | 4 | a file must exist | M4c |

**Takeaway:** support **3 engines** (`pattern` + `presence` + `absence`) and KEUR
covers **~99%** of all rules (1982/2008 directives). The remaining 4 engines (26
directives) are a small long-tail for M4c. So "take over all of cpm's rules" is
realistic — it is not 7 hard problems, it is one (pattern) + two easy ones.

By category the corpus is: quality 281, security 273, a11y 132, secrets 80,
supply-chain 72, build 45, style 29, ai-ml 25… → per the scope doc, **security +
secrets + supply-chain + ai-ml(slop)** gate; **quality + a11y + style** are advisory.

By severity: 344 error, 480 warning, 180 info → maps cleanly to BLOCK/WARN/advisory.

---

## Milestone 1 — Install that just works ✅🟡

*Goal: `keur hook --install/--global` is rock-solid; hooks fire and gate correctly.*

- **T-101** hook install per-repo (.git/hooks) · ✅ done
- **T-102** hook install global (core.hooksPath, respect existing) · ✅ done
- **T-103** hook status / remove · ✅ done
- **T-104** `keur doctor` — detect which of the 9 proven tools are present, report
  gaps, exit-code health. *The dependency-management brick.* · 🔴
- **T-105** wire `check.sh` to read the **tier scanner list** from config (today it
  is hardcoded) so pre-commit vs pre-push actually run their configured sets · 🟡
- **T-106** install self-test: `keur hook --check` runs a known secret/clean fixture
  through the gate and confirms BLOCK/PASS · 🔴
- **T-107** config hierarchy merge (global → repo → dir) actually applied in check · 🔴

**DoD:** fresh machine → `keur hook --global` + `keur doctor` → commits with a secret
BLOCK, clean commits PASS, deterministically.

## Milestone 2 — Opt-in to proven/native tools 🔴

*Goal: user chooses to install additional proven tools; KEUR uses them when present,
degrades gracefully when not.*

- **T-201** `keur doctor --install <tool>` / `--install-all` — guided install via the
  platform pkg manager (brew on mac, apt/etc. on Linux); never auto-installs silently · 🔴
- **T-202** scanner adapters (thin, normalise → Finding): gitleaks 🟡, trufflehog,
  trivy, grype, osv-scanner, semgrep, checkov, tfsec, pii ✅, presidio(optional) · 🟡
- **T-203** evidence aggregator + **corroboration**: same fingerprint from 2+ tools in
  a group → confidence bonus (secrets x2, deps x3, iac x2) · 🔴
- **T-204** policy engine over aggregated evidence (block-at/warn-at, block-categories,
  per-repo overrides) — replace the current grep-for-"block" stub · 🔴
- **T-205** JSON + JUnit + (later) SARIF output, reusing the consolidated-scan contract · 🔴
- **T-206** tier budgets enforced (time-box pre-commit; skip slow tools when over budget) · 🔴
- **T-207** **easy, auditable FP suppression** — inline `keur:ignore <RULE-ID>` on the
  offending line (taught in each rule's own message), so a false positive is a
  one-line, in-diff, reviewable escape hatch. NOT a config file people forget. · 🔴
- **T-208** repo-level suppression + baseline: `.keur/allowlist` for accepted
  findings (accept existing debt once, block only NEW) + `disabled`/`advisory`
  promotion per repo (the `.config/scan.toml` blocking/disabled pattern) · 🔴
- **T-209** suppressions feed the corpus: a `keur:ignore` that is really a FP can be
  promoted into keur-eval `expect_not`, so the FP is measured + never returns · 🔴

**DoD (updated):** `keur doctor --install-all` sets up the toolchain; the gate runs
the right tools per tier; corroborated findings score higher; missing tools skip;
**a false positive is suppressible in one auditable in-diff line.**

## Milestone 3 — Train to reduce native dependency 🔴

*Goal: the self-improving evaluator loop, so KEUR's own detection catches up and we
lean less on the heavy native tools over time. The benchmark decides, never the LLM.*

- **T-301** generalise the proven `multi-scan-eval.sh` into `keur eval` (Ollama
  ground-truth + agreement metric + redacted JSONL + safety controls) · 🔴
- **T-302** corpus splits (train/validation/evaluation/regression) with ground-truth
  labels; **evaluation set immutable + never shown to Ollama** · 🔴
- **T-303** acceptance gate (hard constraints) + weighted objective + Pareto report · 🔴
- **T-304** iteration history (`eval/history/index.json`) + two-layer memory (short +
  compact long-term) · 🔴
- **T-305** Ollama proposer: analyse FP/FN → smallest change → patch + rationale;
  never sets the verdict · 🔴
- **T-306** confidence calibration learned from the corpus (so "0.93" ≈ 93% correct) · 🔴

**DoD:** a run improves precision/recall on validation, passes the acceptance gate,
holds up on the held-out evaluation set, and is recorded — reproducibly.

## Milestone 4 — Absorb cpm's full rule corpus 🔴

*Goal: KEUR consumes all ~1004 cpm rules, routed by intent (gate vs advisory).*

- **T-401** (M4a) `keur-rules` engine: `pattern` engine with target filter
  (extensions, exclude_paths, content_contains) + `cpm:ignore` support → covers 1712
  directives (~85%) · 🔴
- **T-402** (M4b) add `presence` + `absence` engines → +270 directives (~99% total) · 🔴
- **T-403** (M4c) long-tail engines: file-presence/absence, block-absence,
  extract-duplicates → 100% · 🔴
- **T-404** rule import: point KEUR at the cpm rules dir (or vendor a snapshot);
  map category+severity → gate/advisory + BLOCK/WARN/info per the scope table · 🔴
- **T-405** private/APS gap-filler rules kept in KEUR only (never public cpm), e.g.
  the internal-hostname patterns from pii-sensitive-scan · 🔴
- **T-406** feed advisory findings into the AI-input filter (shrink diff → few regions
  worth sending to an LLM) · 🔴

**DoD:** KEUR runs the full cpm corpus; security/secrets/supply-chain/slop gate,
quality/a11y/style are advisory; per-repo config can promote/demote.

---

## Suggested working order

1. **T-104 `keur doctor`** — the missing MVP brick (dependency management). Small,
   isolated, makes the tool honest about what it can do.
2. **T-105 + T-204** — make the gate actually config-driven with a real policy engine.
3. **T-202/T-203** — the remaining adapters + corroboration (the "proven baseline" payoff).
4. Then M3 (train) and M4 (absorb rules) as larger tracks.

## Explicitly NOT in the MVP (M1+M2)

Self-improving loop (M3), full rule corpus (M4), AI escalation, SARIF/dashboard,
commit-msg hook (conventional-commit + no-WIP is a nice post-MVP add via T-1xx).
