## v0.2.0 (2026-10-25)

_Changes since v0.1.0._

### Features
- external scanners are opt-in; default gate is keur-rules only (ADR-066)
- offer keur as a GitHub Action + GitLab CI/CD component (ADR-062)
- KEUR-INT-001 blocks internal references (pre-commit)
- KEUR-DOC-* doc-standards self-checks (ADR-061)
- skip_emoji strip-variant + gitignore-aware file-presence
- add 'keur analyze literals' command + generic class-table rule (ADR-058/059)
- literal-analyze engine — extract/count/classify over a fileset (ADR-060)
- add Go quality rules (idiom/pattern/struct)
- parse generic rules + skip them in standalone dispatch (ADR-059/060)
- literal classifier foundation + PII rename (#18)
- per-rule skip_binary opt-out for binary content
- path-scoped rule disabling via [scopes."<glob>"] in keur.toml
- branch protection — warn/block direct commits to default branch
- 4A tests (Arrange/Act/Assert/Annihilate) + RAII TempDir
- NODE-PIN-001 — warn when a Node project pins no Node version
- .config/keur.toml location + require a repo config by maturity
- base64/url/hex unwrap-then-scan engines (ADR-049)
- flag bidi, zero-width, data-uri, archive and binary artifacts
- QUAL-079 path breakout (absolute/home path, deep ../ traversal)
- colour hook output; AAA demos under docs/demo with a runner
- secure install.sh + release checksums + rule-id control (#15)

### Fixes
- derive KEUR_HOME from the binary, not a required env var (ADR-063)
- SECRETS-057 Vercel token — drop overfit [A-Za-z0-9]{24}
- file-level rules scan the repo, not just the staged diff
- make EX-SEC-010/011 fire on real Express apps
- write PASS/WARN to stdout so editors do not read it as an error
- rule title must not carry a literal backslash-u (arm32 build break)
- global stub must exit 0 when repo has no .githooks hook

### Refactoring
- central Severity enum + from/to-string for drift protection
- central Action enum + from/to-string for drift protection (ADR-058)
- rename keur_test.cpp to tests/unit_test.cpp (split step 4)
- move Layer 4/5 unit suite to its own TU (split step 3)
- move integration suite to tests/integration_test.cpp (split step 2)
- split doctest main + perf suite into own TUs (split step 1)
- extract shared test helpers to tests/test_helpers.h (split step 0)
- cut unwrap complexity/nesting, keep size compares in size_t

### Documentation
- make KEUR-INT-001 generic — shape-based, no hardcoded names
- show the demo GIF + add badges for the public front page
- ADR-065 async build-confidence — fast sync gate, heavy builds async
- ADR-064 pair keur's gate with an AI-guidance layer (arcane)
- scrub internal references before going public
- ADR-061 doc-standards + chapter contract + CMMI doc-ladder
- add ADR-056 (orchestrate clang-tidy for semantic checks)
- add ADR-052 (scan scope) and ADR-053 (finding clustering)
- add ADR-051 (rules distribution) + generated ADR index
- take the macOS/Windows/arm32 matrix off the per-PR hot path
- ADR-050 — act is too slow (cold start) for routine local runs
- ADR-050 CI pipeline ordering (lint-first, e2e as a named stage)
- ADR-049 accepted + authoring guide and test-split TODO
- category-disable (disabled_categories) + PII off-switch discoverability
- ADR-048 — DOOM cheat codes as the suppress/navigate verbs
- ADR-048 — four ignore moves as distinct pacifist actions
- ADR-048 — level is any graph keur can walk (pipeline, IaC, docs, dead code)
- ADR-048 — traceability links are the map, accessibility sets playability
- ADR-048 keur doom — gamified remediation
- ADR-047 verb registry, @shows/@explains, demos

### Build
- set line width to 140 across .clang-format and .editorconfig
- declare C++ style in .clang-format (Google, indent 2, col 100)
- add .editorconfig matching the rkristelijn project convention
- local pipeline pre-flight (scripts/ci-local.sh) + act notes in ADR-050

### CI
- tier the pipeline by cost — Linux-only on PRs, full matrix on main

### Tests
- generic-rule parse/skip + Action conversion (248 unit, 65 integration)
- run stub tests shell-free (fork+execvp, no std::system)

### Chores
- post-public cleanup, defaults/ rename, and e2e/perf hardening (#20)
- post-public cleanup, defaults/ rename, and e2e/perf hardening (#20)
- clean unused includes + add lint-includes make target & CI gate

# Changelog

## v0.1.0 (2026-10-06)

_Changes first release._

### Features
- keur doctor, Makefile conventions, one-click release (#14)
- ~/.keur/rules personal private-rules layer; drop pii shell-scanner from config
- SONAR-001..008 — validate sonar-project.properties
- filename-case naming-convention engine + markdown rule
- NestJS/Fastify/Node security rules + multiline exclude
- refresh state on lens switch after save + '*' deviation marker
- fuzzy / search filter + a-z sorted tree
- keur tune — Tab/Shift-Tab lens router over level/settings/manager
- KEUR-CPP lambda composite + ADR-040 engine gaps
- split train from learn + optimise loop enrichment
- loader + pipeline wiring + explain command
- align all rules to ilities + import full cpm corpus
- ility backfill + cpm corpus import
- ility-aware difficulty resolution in the engine
- NestJS hardening + session-security rules (from mestjs authored GT)
- rules from authored-GT review + fix dead SEC-038 on JS/TS
- authored ground-truth pipeline + tool benchmark (ADR-030/031/032)
- self-explaining findings + layered suppression (ADR-035)
- JS/TS + NestJS/Next.js antipattern rules from mestjs authored GT
- seed-injected learning as the default, all-night resilient
- backfill maturity across the rule corpus (ADR-026)
- wire rule maturity to the chosen level + keur level --global
- scale logo on height too + rename levels 4/5
- DOOM-style 'Choose your Difficulty' skill-level screen
- shell strict-mode + curl-timeout quality rules
- dispatch external tools + project-local check through the gate
- KEUR-PORT-007 — flag emplace_back on aggregates (macOS libc++ construct_at)
- add mestjs to corpus + 5 more ground-truth tools (osv/grype/bandit/njsscan/tfsec) + framework research docs
- LLM ground-truth triage — flag proven-tool false positives (ADR-027) with safety cap + human-review overlay
- ADR-027 file-granularity measurement so the loop sees within-category gaps
- SEC-040 detects NestJS enableCors wildcard/reflection; ignore agent-mailbox
- teach the policy oracle to weigh confidence before it slams the gate
- extract-duplicates — the last cpm engine; + ADR-026 maturity
- detect commonly-committed-by-accident files
- KEUR-QUAL-007 — block committed git conflict markers
- max-dir-files engine + KEUR-QUAL-006; retire check-basics.sh
- max-lines engine + KEUR-QUAL-005; slim check-basics to dir-count
- max-nesting engine + KEUR-QUAL-004 (SonarCloud S134)
- interactive tree TUI for hooks, rules & scanners
- 4 hand-written rules + learn-loop analysis & synthetic-corpus ADR
- promote learned PHP command-exec rule to security/
- path-derived engine + config/hooks/scanners overview
- Dockerised SonarQube helper for ground-truth runs
- optimiser retry loop with dynamic feedback + report tooling
- CICD/IAC/SAST/CMDI/SQLI rules, composite + self-check
- multiline window + ADR-009 facts/requires/composite
- promote 4 reviewed learned rules; preserve the rest as candidates
- KEUR-SEC-006 adapter shell-sink control; drop SEC-005 context-free heuristic
- keur-eval extender — knowledge adapters + generation pipeline
- 32-bit build + post-compile smoke, word-size guard (ADR-006)
- OS abstraction layer for cross-platform build (ADR-005)
- SARIF 2.1.0 output format
- import cpm secrets/security/supply-chain pattern rules (breadth)
- rule-level message + block-absence engine (ADR-004 stage 2)
- inclusive-language rules (KEUR-INCL-001..003)
- file-absence / file-presence engines (ADR-004 stage 2)
- Stage 1 — tokenizer + skip_comments/skip_strings (ADR-004)
- run-until-stopped loop mode for overnight runs
- --source alternate (train corpus + keureval, flipping per phase)
- plain-terms summary, resource/duration readout, --model flag
- plain-terms summary in the monitor
- cpm as ground-truth source + composite-candidate store
- project rule layer (.keur/rules) + keur rules list
- global hook orchestrator with composable modes
- keur-eval contract source, slop marker, gate calibration
- keur learn — background optimise loop + responsive TUI monitor
- AWS Security Agent report adapter (third ground-truth source)
- promote 3 optimiser-learned rules to production (reviewed)
- rotate FN targeting across missed clusters
- prime Ollama with rule-engineering expertise (keur-rule-engineer)
- progress.sh — readable optimiser progress view
- convergence detection in the optimiser loop
- repo-level holdout split + anti-overfit guards
- repo+category matching makes the loop actually learn
- optimiser loop — Ollama proposes rules, benchmark gates
- ground-truth cache + keur-vs-baseline measurement
- resource guards + babysittable background supervisor
- real-repo corpus + proven-tool ground truth (libmode start)
- --format/--color output flags + full docs set
- single C++ binary with full test pipeline
- measurement loop + metrics logging + FP-suppression tickets
- proven-tech baseline + first adapter (PII)
- scaffold KEUR — deterministic git-hook gate for AI-generated code

### Fixes
- grey repo-overruled global rows live, incl. fully-overruled parents
- pin third-party actions to commit SHA (Sonar security)
- explicit lambda captures + split double-if; drop all-5 keur.toml
- const logo pointers + bounded ESC escapes (Sonar security)
- validate CLI paths in benchmark-score + control rule
- use [[ ]] instead of [ ] (KEUR-QUAL-003 / Sonar)
- no config = no filter (back-compat) + follow KEUR-PORT-007
- explicit Row ctor so emplace_back builds on libc++
- constructors for HookManager, ScannerConfigFile, RuleFinding
- explicit Layer constructor for macOS libc++ emplace_back
- explicit constructors so emplace_back builds on macOS libc++
- mark safe std::pair emplace_back sites as keur:ignore KEUR-PORT-007
- push_back({...}) for Row aggregate in collect_visible (macOS libc++)
- push_back({...}) for all constructor-less aggregate structs (macOS libc++ construct_at)
- push_back({...}) over emplace_back for aggregate structs (macOS libc++ construct_at)
- stop the comment-stripper from also swallowing string literals when a strings-rule shares the room
- revoke the open-door policy on scratch directories, and post a guard rule so it stays shut
- leash the remaining lambdas that were eyeing the whole scope
- pin down the lambdas before they capture the whole galaxy by reference
- extend [[ ]] fix to test scripts, widen KEUR-QUAL-003 scope
- use [[ ]] over [ ] in bash scripts + add KEUR-QUAL-003
- bound monitor box height + richer session header/log
- shell-free run_argv; migrate learn + hook off interpolated shell strings
- shell-free tool_on_path + KEUR-SEC-005 command-injection control
- scan a single file, not just a directory
- harden the learning gate + expand the ground-truth baseline
- move imported cpm rules out of the training scope
- semgrep adapter (heredoc stole stdin) -> 4x richer ground truth

### Performance
- skip keur's own generated data dirs (eval/corpus, eval/history)

### Refactoring
- testable handlers + shared reducers, drop dead code
- spell the glyphs out loud instead of hiding them behind numeric escapes, with a guard rule
- evict the throwaway temporaries and the roundabout membership checks, with guards to keep them out
- merge rules-imported/ into rules/ via a source: field
- flatten >3 nested blocks (SonarCloud S134)
- split rule_engine.cpp into rule/{parse,match,scan} (SRP, <300)
- split cmd/learn.cpp into learn/ layer (SRP, <300 lines)
- nested namespaces mirror the folder layers + enforce it
- drop redundant cmd_ prefix from files in cmd/
- folder structure + self-enforcing architecture control
- spoofable fs + proc adapters (complete provider set)
- provider/adapter architecture (codeflex)

### Documentation
- confirm finding double-count is general (not SONAR-only) — flupke probe fired 2x
- ADR-042 — self-explaining PII-vault scanner ($PII_VAULT); note the gate regression
- add SECURITY.md — private vulnerability disclosure (closes KEUR-QUAL-002)
- document engine/app split + two-Makefile rationale; close TODO structure Qs
- ADR-041 — tiered CI (depth × breadth); defer arm32 QEMU to merge
- sync chooser-ui + TODO with the shipped keur tune
- ADR-037 — AGENTS.md canonical + CONTRIBUTING link + managed-block sync
- difficulty-resolution contract (DESIGN-DIFFRES)
- ADR-039 rule ility-alignment + ISO 25010 difficulty model
- add ADR-037 (agent briefing) and ADR-038 (per-dimension difficulty)
- ADR-030..032, 035, 036 — GT trust, validation, self-explaining, train/learn split
- add JS/TS top-50 antipatterns catalog
- ADR-028 — sunset cpm, keur is the successor (hook + pipeline)
- teach the LLM prime and authoring guide the maturity field
- trace ADR-027 to its measure.sh implementation
- close coverage gaps to 100% (strict green)
- ADR-018 — cpm as a second quality gate over keur (parked)
- ADR-017 — surfacing traceability/doc/test-coverage metrics (parked)
- @implements ADR-003/ADR-005 on adapters, platform, report
- ADR-009..016, vision, traceability + doc/pipeline coverage tooling
- rule-authoring reference + updated model priming
- extract-duplicates engine queued; SARIF marked done
- ADR-004 analysis-engine evolution + lessons from existing tools
- composite rules via corroboration, learn provider pattern, slop notes
- keur configure REPL design note; TODO updates (slop, TUI, arch)
- record first successful run + review workflow in learning-loop
- remove em-dashes from HOWTO-keur-eval (repo-wide no-em-dash style)
- document the learning loop + findings
- mark output flags + docs track done in TODO
- update TODO.md — mark codeflex/structure/ADR work done, add next-up order
- 4 milestones + tickets + rule feature-coverage analysis
- sharpen framing — cpm=analyzer/pipeline, KEUR=hook manager
- define KEUR scope — guard not coach

### CI
- exclude generated artifacts + vendor from analysis
- gate keur's own diff with keur check

### Tests
- drop /tmp literal from parse tests (SonarCloud cpp:S5443)
- locale-independent braille check (byte-level)
- update ility fallback asserts to ISO 25010 names

### Chores
- keur gates its own code at max difficulty (keur.toml)
- banish the ghosts of hardcoded paths back to the void
- keep eval + e2e scripts executable
- remove overfit composite-candidates from the broken-gate run
- check-basics counts logical units (.h+.cpp = 1), now green
- check-basics — file/dir size norms as a lint tool
- system-intel adapter TODO + new composite-candidate from testing
- gitignore compile_commands.json (generated, machine-specific paths)