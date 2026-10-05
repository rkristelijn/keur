# TODO

Status legend: [x] done · [~] partially done · [ ] open

## Done (this cycle)

- [x] Sonar config validation rules (SONAR-001..008): nothing else validates
      `sonar-project.properties` (Sonar silently ignores unknown/missing keys),
      so keur does. SONAR-001 missing projectKey, 002 missing organization
      (SonarCloud), 003 token/login committed (secret-in-config), 004 misspelled
      keys (exclusion/projectkey/reportPath), 005-008 per-language coverage path
      MISSING via `requires: lang` + `absence` (java→jacoco, js/ts→lcov,
      python→coverage.xml, go→coverage profile). Validated by
      tests/e2e/test_sonar.sh (8 asserts: true positives, 0 FP, requires: gating).

- [x] difficulty TUI (ADR-038/039, docs/design/difficulty-chooser-ui.md): the
      three lenses over one keur.toml state. `keur tune` router switches lenses
      with Tab/Shift-Tab; `keur level` (coarse) steps in on Tab; `keur settings`
      (medior ISO 25010 sliders) and `keur manager` (expert tree) also run
      standalone. Shared load/save path (single `.bak`), `*` deviation marker on
      level, lens-switch refresh after save.
    - [x] `keur manager`: repo-overrules-global greying (incl. fully overruled
          parent groups), `/` fuzzy filter (keeps ancestor path, collapsible),
          a-z sorted tree (Scanners/Hooks pinned on top).
    - [~] DEFERRED: `*` override marker inside `keur manager` (does not map
          cleanly onto per-rule rows — the tree already shows "overruled by
          repo" + disabled). Multi-step undo = manager Layer 2 (single `.bak` now).

- [x] analyse the literals / duplicate stuff — centralised: git+fs+proc adapters,
      one report/output provider, one TOML reader. (ADR-003)
- [x] see repeating patterns, workdir/git queried multiple times — one Git adapter
      (single git touch point), one Fs adapter, one Proc adapter.
- [x] SRP violations — policy_evaluate is pure; orchestrators take injectable
      providers; I/O isolated in adapters/.
- [x] output stuff centralised so we can have themes — report provider does
      jsonl / compact / verbose + themeable colour (TTY / NO_COLOR aware).
- [x] do we need all files/folders in the root, folder structure for C++ —
      src/keur split into cmd/ adapters/ report/ util/; namespaces mirror it.
- [x] document implicit design choices — ADR-002 (single binary), ADR-003
      (provider architecture + enforcement).
- [x] BONUS: self-enforcing architecture controls — KEUR-ARCH-001 (I/O only in
      adapters), KEUR-ARCH-002..004 (namespace matches layer), verified by
      tests/e2e/test_selfcheck.sh (dogfooding).

## Partially done

- [x] output consistent / json / compact / verbose / colour — report provider
      supports all modes AND `keur scan` exposes `--format jsonl|compact|verbose`
      and `--color auto|always|never` (one shared parser). Still to wire: same
      flags on `keur check` if a human-readable gate report is wanted.
- [~] cleanup root folders — src/keur is clean. VERIFIED 2026-10-05: the dead
  dirs are already gone (test/ optimiser/ policy/ lib/ hooks/ no longer exist).
  scanners/ REMAINS and is NOT dead — pii.sh/rules.sh are still referenced by
  config/keur.toml + src/rules/main.cpp (the script-scanner seam, see the
  libmode item). rules/ (data) vs src/rules/ (engine) now CLARIFIED in
  src/README.md. Still open: decide on config/ -> .config/.

## Open

### Features

- [x] personal cross-repo private-rules layer: `~/.keur/rules` added to the rule
      dir cascade (keur_utils.h) — a fixed home for private rules outside every
      git tree (git can't stage it, scan-walker never reads it), silently skipped
      when absent. Unit-tested. Documented in rules/README.md. (2026-10-05)
- [ ] native `$PII_VAULT` support — the REAL cross-repo BII/PII denylist path.
      DESIGNED: see docs/adr/ADR-042-pii-vault-scanner.md (opt-in, self-explaining
      scanner that reads the vault and tells you where it goes when you turn it on).
      URGENT CONTEXT (2026-10-05): the PII gate SILENTLY VANISHED — the git hooks
      now `exec keur check` only and no longer source ~/.config/git/hooks/lib/
      no-pii.sh (it lies dead in lib/), so NOTHING blocks a BSN/plate/internal-
      hostname from being committed in keur right now. A central vault already
      exists at `~/.local/share/pii/patterns.d/*.pii` (cpm + the old hook read it
      via $PII_VAULT). Manual proof done: 588 vault patterns → temp ~/.keur/rules
      rule → scan keur = 0 matches AND the engine matches the format fine, so this
      is wiring, not capability. Implement ADR-042's follow-ups 1-6.
- [ ] remove the dead script-scanner shims: `scanners/pii.sh` + `scanners/rules.sh`
      (no longer referenced by config after this session). `eval/evaluate.sh` still
      calls them behind `[[ -x ]]` guards (won't break, but is a dead scaffold —
      point it at bin/keur-rules or retire it). Small, separate cleanup.

- [ ] config-in-root hygiene rule: flag a config file sitting in the repo root
      when the tool supports a non-root location (e.g. keur.toml → .config/
      .keur.toml, which keur already resolves). Must be DATA-driven (a list of
      tools that support .config/) so it never flags root-only files that CANNOT
      move (sonar-project.properties, mull.yml, compile_commands.json, .gitignore
      are tool-hardcoded to root). Needs a design decision on which tools to
      include. (raised 2026-10-04 alongside the SONAR-* rules.)
- [ ] findings double-count per (rule,file,line) — each fires 2× in a scan.
      CONFIRMED 2026-10-05 it is NOT SONAR-specific: a one-off pattern rule
      (PII-vault 'flupke' probe) also fired twice on the same line:1. So it is a
      general dedup gap in the scan/report path, not a per-rule quirk. Fix: dedupe
      on (rule,file,line) before emitting findings. First seen on SONAR-* rules.

- [ ] extract-duplicates engine (last of cpm's 7 engines; ADR-004 stage 2). Per
      file, extract a capture group via `extract:` regex + `capture:` index and
      flag duplicate captured values (e.g. duplicate test names in one file).
      Needs fields extract/capture/message and an eval_extract in rule/. cpm ref:
      adr-166 phase 5. (NOTE: the file/dir-size aggregation that check-basics.sh
      did is already ported — max-lines + max-dir-files engines, KEUR-QUAL-005/006;
      the script is retired. This item is now only the extract-duplicates engine.)
- [ ] KEUR_HOME should be DERIVED, not a required global env var. Today keur
      hard-reads getenv("KEUR_HOME") in 4 places (util/keur_utils.h rule_dirs,
      cmd/policy.cpp resolve_config, cmd/learn.cpp keur_home, src/rules/main.cpp)
      and derives its own location NOWHERE. If the user has not exported it,
      keur finds no rules (empty `keur rules list`); if it is set wrong it shifts
      config resolution. Fix: resolve keur's home automatically —
        1. from the binary path (argv[0] → resolve symlinks → parent of bin/),
        2. or walk up to a config/keur.toml / installed marker,
      and treat $KEUR_HOME only as an explicit OVERRIDE. Makes it optional like
      git/.git, cargo, etc. Removes the setup drift that made tests env-fragile.
      (The .keur/rules cascade already reduces this for project rules; this
      finishes it for the built-in layer.)
- [ ] libmode: run external scanners (semgrep, gitleaks, trivy, ...) via the
      Proc adapter, OR ruleengine mode, or both — normalise into one findings
      report. (ADR-019.)
      REALITY CHECK (2026-09-20): the Proc seam (run_argv shell-free + capture)
      is ready AND config/keur.toml already lists every [scanners.*], but
      cmd/check.cpp dispatches ONLY the built-in RE2 rule engine today — no
      external-tool or script scanner is invoked yet (the `pii` script-scanner in
      config is config-ahead-of-code). So the real prerequisite is a
      scanner-DISPATCH loop in check(): read the [scanners.*] inventory, run each
      enabled adapter over the staged files, normalise stdout → Finding, feed the
      one policy engine. Once that loop exists, both shapes below are thin.
      - [ ] shape A: proven-tool adapters (shellcheck first — covers the SC2034
      class that slipped past keur locally but failed GitLab CI in
      security-tooling; then semgrep/gitleaks/trivy/checkov). PATH-resolved,
      missing → skip. Shared coverage across all repos.
      - [ ] shape B: project-local adapter — run a git-TRACKED `.keur/check.sh`
      (fallback `.config/keur/check.sh`), staged files on argv, JSONL on
      stdout → Finding. Trust model DECIDED (ADR-019): not config-opt-in;
      tracked-only execution (untracked/dropped scripts ignored) + shell-
      free spawn (run_argv) + confidence cap (~0.80) so a repo can't
      self-BLOCK above calibrated tools. Config `enabled=false` still
      disables entirely. Thin runner + JSONL parser over the dispatch loop.
- [ ] ollama learn/loop as designed (eval/ harness exists; optimiser missing).
- [ ] surface traceability / doc-coverage + test-coverage metrics on the
      dashboard (ADR-017). Decide Route A (own CI gate: doc-coverage.sh --strict
      as a PR check — small, fits "keur guards itself") vs Route B (sonar-scanner
      + sonar.externalIssuesReportPaths — one dashboard, but migrates off
      autoscan, needs SONAR_TOKEN + plan-feature check). Parked pending the
      choice. See docs/adr/ADR-017-surfacing-coverage-metrics.md.
- [ ] wire cpm in as a second quality gate over keur's own code (ADR-018).
      cpm's mature checks already find ZERO issues on keur's source (good
      regression gate), but cpm scans the corpus fixtures too. Prereq: solve the
      corpus-exclude (eval/corpus/repos/ + the cpm-sourced rules, now
      `source: cpm` within rules/) — the exact cpm exclude mechanism is
      unconfirmed. Then add cpm.toml + optional CI job.
- [ ] SCAN DISPATCH: a rule with an `extract:`/file-metric engine but NO
      `patterns:` block does not fire — the per-file dispatch only runs a rule
      that reached `filtered`, and a no-patterns rule is dropped from the
      candidate path. Workaround in place: the 3 imported extract-duplicates
      rules carry a dummy `patterns: - regex: "{extract-duplicates}"` block so
      they dispatch. Proper fix: let engines that don't consume `patterns`
      (extract-duplicates, max-lines, max-dir-files, max-nesting) dispatch on the
      rule's own field (extract/min_count) without requiring a patterns block.
      See src/rules/rule/scan.cpp (candidate/filtered path).
- [ ] IMPORT THE FULL CPM CORPUS + MATURITY GATING (ADR-026). ~811 more cpm rules
      use engines keur now supports and could be imported (source: cpm). But
      shipping ~1000 rules all-active is far too strict. First implement ADR-026:
      a `maturity:` field per rule (1 prototype .. 5 optimising) + profile
      templates under config/profiles/ (prototype/startup/standard/hardened/
      mission-critical) that activate rules with maturity <= the project's level
      (ADR-011 cascade). THEN bulk-import with maturity seeded from cpm severity/
      category. No engine change (rules stay data). Backfill maturity on the
      existing corpus too (deadly rules → 1, opinionated/framework → 5).
- [ ] PERF: eval/doc-coverage.sh feels slow — investigate + speed up. It greps
      tags across src/ rules/ docs/ eval/ repeatedly (multiple full-tree greps
      per section, 235+ rule files now). Profile it (which section dominates),
      then cache the tag scan once and reuse, or narrow the scan set. It runs as
      a gate, so sub-second matters.
      Bonus: feed multiline/count engine support upstream to cpm to drop the
      "unsupported engine" warnings. See docs/adr/ADR-018-cpm-second-gate.md.
- [ ] NO PYTHON in the eval scripting layer — replace inline `python3 -c` JSON
      writers with either pure shell OR a small standalone compiled C++ util.
      Rationale: keur's identity is one compiled binary, no runtime language dep;
      adding Python is a third language (bash + C++ + Python) with its own
      boilerplate/runtime just to append a JSON line. Today `eval/optimise.sh`
      uses inline python3 heredocs in record() (and elsewhere: measure.sh,
      alternate.sh) to emit JSONL. Decision (2026-09-20): prefer a tiny
      `keur-jsonl` C++ util (argv → one JSON line, json-escaped) compiled in the
      same toolchain as keur-rules — testable, no heredoc per callsite, robust
      for regex payloads with quotes/backslashes. Wire record() + log_attempt +
      the measure/alternate emitters through it, then drop the inline python3.
      (Blocks the per-attempt logging below from being "clean" — until the util
      exists, keep attempt logging minimal / shell-only.)
- [ ] DYNAMIC retry feedback for the optimiser retry loop. The retry loop
      (KEUR_MAX_ATTEMPTS, added 2026-09-20) now re-prompts Ollama on the SAME
      missed case with the reject reason + a hardcoded per-category good example
      (good_example_for in optimise.sh). That steering is STATIC. Make it
      dynamic: (a) log every attempt (iter, attempt#, category, outcome, reason,
      the regex produced) to eval/history/attempts.tsv so we can SEE how the
      model does across retries and WHY it is rejected; (b) use that data to
      tailor feedback — e.g. if the model keeps overfitting, show the actual
      diff between its literal and a generalised form; if precision drops, name
      the safe lines it wrongly hit. Depends on the no-python decision above for
      the attempt log writer. Also consider feeding a RANDOM real good rule of
      the same category as an extra worked example on retry (user idea 2026-09-20).

### Ground-truth quality (2026-09-17 session — see docs/design/ground-truth-roadmap.md)

- [x] add trufflehog adapter to eval/groundtruth.sh (secrets, JSONL, offline).
- [x] dedup/combine findings in eval/buildgt.sh on (repo,file,category) with a
      `tools` list + `seen_by` count (clean stats + confirmation signal).
- [x] audit filters in eval/measure.sh: KEUR_GT_TOOLS (proven-tool subset) and
      KEUR_GT_CATEGORIES (category scope, applied to BOTH sides).
- [x] FIX gate precision-floor bug in eval/optimise.sh: the accept test used
      `min(FLOOR, b_p)`, which LOWERED the floor once precision had dipped below
      it → accept-everything spiral (overnight run reached recall 1.0 while
      precision fell to 0.4). Now a hard floor + no regression below baseline.
      Verified with scenario tests (overfit p=0.4 now rejected; good rule kept).
- [x] expand the corpus: 19 repos now (added WebGoat, crAPI, OWASPWebGoatPHP,
      Pixi, VyAPI + clean OSS flask/express/gin/requests/sinatra). 2 candidates
      failed to clone (payloadbox/sql-injection-payload-list, OWASP/DVGA).
- [ ] BUILD OUR OWN vulnerable Next.js corpus repo (no good off-the-shelf one
      exists, and Next.js is a common, high-value target). A small, self-contained
      "next-goat": intentionally vulnerable Next.js (App Router) app covering the
      shapes keur should catch — server actions / route handlers with unsafe
      input, SSRF via fetch to a user-controlled URL, hardcoded secrets in
      env/config, `dangerouslySetInnerHTML` XSS, open redirect, missing authz on
      an API route, and a leaky `.env`/`next.config`. Keep it --depth-1-small
      (<100MB, well under the corpus cap). Two ways to use it:
      (a) commit it as its own tiny repo and add its slug to repos.txt, or
      (b) generate it under eval/corpus/repos/ as a local fixture (no clone).
      Because the ground truth is source-agnostic, we can either run the proven
      tools over it OR hand-label the planted issues (we know where we put them)
      — a rare case where we OWN the ground truth exactly. Pairs well with the
      TS/Node/GraphQL coverage already in the corpus. (idea 2026-09-23)
- [x] more ground-truth tool adapters: trivy + checkov added (category `iac`).
      Ground truth now 683 combined findings from 5 tools (semgrep/gitleaks/
      trufflehog/trivy/checkov) across 21 repos. Clean security-scope baseline:
      recall 0.81 / precision 0.76 / F1 0.78.
- [ ] API-based ground-truth sources: GitHub Copilot, CodeRabbit, Sonar
      (same one-adapter pattern; these need API access, not local binaries).
- [ ] combination analysis BEFORE training: score every tool set as TP/FP/TN/FN;
      weight confidence by `seen_by` (multi-tool agreement = trustworthy;
      single-tool hits = scrutinise — they were the main noise source).
- [ ] LLM-as-a-judge: ask Ollama (code snippet + finding) whether a finding is a
      genuine TP or a FP, to PRUNE mislabelled ground truth (e.g. the
      InvoiceController.java "sqli" false positive). Hard rule: the LLM may judge
      the BENCHMARK, never approve rules — rule acceptance stays with the
      deterministic gate.
- [ ] "can't do it without this feature" signal from Ollama: when the model
      cannot write a generalising rule for a missed case with the CURRENT engine
      (pattern/presence/absence), let it emit a structured suggestion naming the
      engine capability it WOULD need (e.g. multi-line context, cross-file
      correlation, AST/semantic match, extract+capture). Aggregate these across
      all missed cases → a ranked "engine feature demand" list, so the rule
      engine gets extended by POPULAR DEMAND (the misses that keep needing the
      same capability) rather than guesswork. Ties into ADR-004 (analysis-engine
      evolution) and the extract-duplicates engine already on the list. The
      suggestion is ADVISORY only — it never bypasses the deterministic gate.
- [ ] make eval/keureval.sh honour KEUR_GT_TOOLS / KEUR_GT_CATEGORIES too, so
      `--source alternate` trains both signals against the same clean scope
      (today only the corpus source via measure.sh applies the filters).
- [ ] COMPOSITE RULES via corroboration (supersedes the slop soft-marker).
      Insight: several SIMPLE selectors that fire together are more reliable than
      one big complex regex. A "slop" rule on its own (e.g. an `internal
      hostname:` label, or a bare private-IP pattern, or a money figure) is a
      weak signal; but 2-3 of them CORROBORATING in the same file/block is a
      strong signal of real business-sensitive data. So slop rules are not junk
      — they are BUILDING BLOCKS.
      keur.toml already DESIGNS this but the policy engine does not implement it:
      corroboration-bonus = 0.10 corroboration-cap = 0.99
      escalate-band-low = 0.40 escalate-band-high = 0.75
      Plan:
        - weak selectors sit in the ambiguous band (WARN-level) on their own;
        - when N selectors co-occur (same file/block, or a shared group: field),
      add corroboration-bonus per agreement, capped, lifting combined
      confidence over block-at -> BLOCK;
        - source of building blocks: eval/composite-candidates/ (slop rules are
      preserved there now, NOT loaded standalone, so no WARN noise);
        - needs: policy_evaluate() to combine findings (currently binary per
      finding), a grouping mechanism (group: field or co-location), and the
      confidence arithmetic.
      This is the CONSTRUCTIVE answer to slop (make weak rules useful in
      combination) vs the current DEFENSIVE one (degrade weak rules). It is
      policy-engine work — do it as a deliberate step, not a quick patch.
- [ ] pluggable private rules: loadable (KEUR_RULES_DIRS works) + docs + man page
      in the binary, so an AI/agent can discover how to add custom rules.
- [x] SARIF output format — DONE (keur scan --format sarif, SARIF 2.1.0). The
      scanning ingest it). From the tool study (docs/design/lessons-from-
      existing-tools.md): every serious SAST tool emits SARIF; add it as a
      report format alongside jsonl/compact/verbose so keur interoperates.
- [ ] more ground-truth adapters, reusing proven tools instead of reimplementing
      their detections: bandit (Python), gosec (Go), brakeman (Rails),
      gitleaks/trufflehog (secrets) — same pattern as groundtruth-cpm/awssec.
- [ ] if/when a structural rule type lands (ADR-004 stage 5): copy Semgrep's
      ergonomics — patterns that LOOK like source code + metavariables ($X) +
      ellipsis (...), not a new query language. Optimise for "a dev with no
      program-analysis background can write a rule".
- [ ] LEARN PROVIDER PATTERN — make the rule-PROPOSER pluggable (default Ollama).
      Today eval/optimise.sh has one hard-coded ollama() function that POSTs a
      prompt to localhost:11434 and gets a candidate rule back. Abstract it to a
      "proposer" interface, keeping Ollama as the default, so the same learn
      loop (measure + gate + slop test stay unchanged, LLM-agnostic) can use a
      stronger model for the hard cases. Mirror the multi-provider design of
      rkristelijn/llama-cli (Ollama + OpenAI-compatible, switchable at runtime;
      config precedence CLI > .env > env > default).
        - selection: KEUR_PROPOSER=ollama|openai|bedrock|kiro (default ollama)
        - providers to support:
            * ollama            — local, free, offline (DEFAULT; the bulk loop)
            * openai-compatible — one adapter covers OpenAI, llama.cpp server,
      LM Studio, proxies (biggest coverage per effort)
            * bedrock — AWS (Claude/Titan/Llama); fits the AWS-sec work
            * kiro — likely best per-rule quality; use for the HARD
      cases (persistent holdout-rejects) where Ollama
      stalls, not for the whole bulk loop
        - rationale: the deterministic gate + slop test already correct for weak
      proposer output, so cheap/local Ollama is right for volume; a stronger
      paid/remote proposer earns its cost only on the cases Ollama can't
      crack. Keep the loop able to MIX: Ollama for bulk, escalate hard FNs.
        - privacy note: remote providers send the corpus cases off-machine;
      gate this behind explicit opt-in (never default), consistent with
      keur's local-first stance.

### Docs (DONE this cycle)

- [x] README with basic user stuff
- [x] CONTRIBUTING with links to the docs
- [x] docs/c4.md with C4 diagrams (Context / Container / Component, mermaid)
- [x] dataflow diagrams (docs/dataflow.md)
- [x] class diagram + sequence diagrams (docs/diagrams.md, mermaid)
- [ ] ability to regenerate the 'model' split from the view (auto-gen) — not done

### Quality / research

- [ ] no .clang-format exists — C++ style is inconsistent hand-formatting.
      Notably, broken function headers align continuation lines under the open
      paren (AlignAfterOpenBracket: Align), which drifts right on long names and
      churns diffs on rename. Add a .clang-format with a STABLE style (e.g.
      AlignAfterOpenBracket: BlockIndent or DontAlign + ContinuationIndentWidth:
      4, one param per line), then format the codebase once. Hook into `make
      lint`/`make format`. New code is already written in the stable style.
- [ ] BASICS as enforced norms (candidate for KEUR-ARCH self-check rules):
      * no source file > ~300 NET lines (comments/blank excluded);
      * no dir > ~10 files;
      * name conventions, logical layout, one clear responsibility per file (SRP);
      * little copy-paste; build once in a lib/module, then reuse.
      Status: file-size violations RESOLVED (learn.cpp + rule_engine.cpp split).
      cmd/ folder count resolved by counting logical units (.h+.cpp = 1 unit).
      check-basics GREEN and now RETIRED into engines (max-lines/max-dir-files,
      KEUR-QUAL-005/006). Historical notes:
      * src/keur/cmd/learn.cpp (~411 net lines, 4 responsibilities: JSONL parse +
      resource read + TUI render + orchestration) — SPLIT into
      learn/history.{h,cpp} (parse), learn/sysinfo (resources; also the
      cross-platform adapter TODO), learn/monitor (render), cmd/learn.cpp
      (orchestration only). This is fresh debt from this cycle.
      * src/keur/keur_test.cpp (~858 net) — split per suite/feature.
      * src/rules/rule_engine.cpp (~358 net) — watch as ADR-004 stages land;
      split engines (pattern/presence/absence/block/extract) into siblings.
      * src/keur/cmd (12 files) and eval/ (21 shell scripts) — over the folder
      cap; regroup (eval/ into subdirs: corpus/, groundtruth/, loop/, report/).
      Consider making these checks KEUR's OWN dogfooding rules once measurable.
- [ ] rules: do they follow an ISO *ilities structure? would that be applicable?
- [ ] CENTRALISE the corpus path (DRY) + move it out of the tracked tree.
      Today the corpus path is hardcoded ~146x across ~8 eval/*.sh scripts
      (corpus/repos, corpus/groundtruth, corpus/measure, corpus/split) — no
      shared config. Fix: one `eval/lib.sh` (or env.sh) that defines
      KEUR_CORPUS_DIR + CORPUS_REPOS/GT/MEASURE/SPLIT as variables; every eval
      script sources it and uses the vars. THEN move the generated corpus into a
      conventionally-ignored dot-dir (e.g. eval/.corpus/ or .tmp/corpus/) by
      changing ONE line — so it's one .gitignore rule (not 4), auto-skipped by
      the scan walker (hidden dir), and tracked manifest (repos.txt) stays put.
      Do it in two verifiable steps: (1) introduce the vars pointing at the
      CURRENT path (behaviour-neutral, bash -n + path-resolves-same per script);
      (2) flip the path to the dot-dir and verify the learn loop still finds the
      same ground truth. Ties into the eval/ regroup item above.
      INTERIM (done 2026-09-23): `corpus` added to the scan walker's skip-dir
      list so `keur scan .` no longer descends into the ~290MB clones.
- [ ] run cpm in extreme mode here + semgrep — dead code, dead docs, links
      between code and docs; all should be green.
- [ ] TUI / a11y / design: are there TUI designs, are we accessible? (see cpm
      knowledge). Tie into the report provider's output modes.

## Next up (recommended order)

1. Root cleanup: delete empty/dead dirs (test/ optimiser/ policy/), remove
   deprecated shell (lib/ scanners/ hooks/), document rules/ vs src/rules/.
   Then run cpm extreme + semgrep green.
2. libmode: external-scanner adapters over Proc, unified report.
3. pluggable rules docs + man page (in the binary, discoverable by an AI).
4. ollama optimiser loop.
5. (optional) --format/--color on `keur check`; model auto-gen for diagrams.

- [ ] slop marker is too strict on CODE cases (false-positive slop). Observed:
      two near-identical genuine cmdi sink rules were classified differently —
      one REAL, one SLOP — because perturb.sh's identifier-rename for a specific
      .py case happened to break the sink match. The perturbation test is solid
      on PROSE (cbi label rules) but wobbly on code. Make code perturbation
      preserve the dangerous-call syntax more carefully (rename only clearly
      user-input identifiers, never touch the sink call token). It is "soft"
      (WARN, not discard) so not urgent, but it mislabels good rules. See
      eval/perturb.sh (code branch) and eval/optimise.sh (is_slop).
- [x] SYSTEM INTEL + all OS-divergent code now behind a platform layer (ADR-005).
      DONE: created src/keur/common/{platform.h,compat.h,platform_posix.cpp,
      platform_win32.cpp} (namespace keur::platform), selected per-OS by the
      Makefile (PLATFORM_SRC). The only #ifdef in the whole codebase lives there
      (a single __APPLE__ split for mach-vs-/proc). Migrated off raw OS calls:
        * sysinfo → platform::read_resources (macOS mach/sysctl, Linux /proc,
      Windows GlobalMemoryStatusEx); adapters::SysInfo stays the
      injectable seam and just delegates.
        * proc/git → platform::run/capture/capture_bytes/tool_on_path/wait_exit
        * fs        → platform::make_dir/path_exists/write_executable/remove_file
        * hook → platform::path_join (git-hook stubs stay #!/bin/sh — legit)
        * learn → platform::sleep_ms / terminal_size / process_alive
      Enforced by KEUR-PORT-001..005 (rules/keur-internal/) + test_portability.sh
      (0 findings on src/keur, fire-detection proven). CI builds+smoke-tests
      linux/macos/windows (.github/workflows/ci.yml). See ADR-005.
- [x] Why do we have multiple Makefiles — DOCUMENTED in src/README.md: two
      layers, two artifacts. src/rules/Makefile builds bin/keur-rules (engine
      standalone, for the eval harness); src/keur/Makefile builds bin/keur (the
      product) and links the engine's translation units in directly (ADR-002,
      single binary — keur does NOT shell out to keur-rules).
- [x] why is rules apart from keur in src? what is the purpose? — DOCUMENTED in
      src/README.md + src/keur/README.md + updated src/rules/README.md.
      src/rules/ = the ENGINE (reusable lower layer, one-way dep: knows nothing
      of git/hooks/config/tty, surface = rule_engine.h). src/keur/ = the APP
      (cmd/adapters/config/report/render/manager/learn) built on top. Separate
      axis from code-vs-data: src/rules/ (engine code) reads rules/ (rule data).
- [ ] `keur learn` TUI: equalise the height of the two side-by-side source
      columns (corpus | keureval). `render::beside()` pads width but not height,
      so the shorter box (usually keureval) ends higher and leaves ragged
      whitespace beside the taller one. Pad the shorter block with blank lines
      to the taller block's height, or draw a continuing right border. Cosmetic
      only. See src/keur/render/box.cpp (beside) and cmd/learn.cpp (draw_frame,
      wide branch).
- [ ] KEUR-ARCH-001 (and siblings) are not comment-aware: the regex for raw
      `system(` matched the word "system(" inside a CODE COMMENT in learn.cpp,
      producing a false-positive BLOCK. The rule engine should ignore matches
      inside line/block comments (per-language comment syntax) so documenting a
      guard doesn't trip the guard. See rules/keur-internal/KEUR-ARCH-001*.
- [ ] `keur learn` TUI: show visible PROGRESS OF THE CURRENT RUN. Right now the
      boxes aggregate ALL history (old + new runs mixed), so when a fresh run
      only produces rejects (recall/accepted unchanged) the panel looks frozen
      even though `iters` climbs and the log line moves. Improvements to make:
        - mark/segment "this session" (records since monitor start, e.g. by ts
      or a run marker) so the user sees what THIS run did;
        - surface a live activity indicator (current iter, target FN, spinner)
      more prominently than the single bottom log line;
        - show a per-run counter (iters this session, accepts this session) and
      "last change N iters ago" so a rejects-only stretch reads as progress,
      not a hang.
      See src/keur/cmd/learn.cpp (draw_frame, aggregate).
