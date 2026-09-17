# TODO

Status legend: [x] done · [~] partially done · [ ] open

## Done (this cycle)

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
- [~] cleanup root folders — src/keur is clean; still to remove: empty dirs
  (test/ optimiser/ policy/), deprecated shell (lib/ scanners/ hooks/),
  and decide on config/ -> .config/. Clarify rules/ (data) vs src/rules/ (engine).

## Open

### Features

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
      report. (Proc adapter seam is ready.)
- [ ] ollama learn/loop as designed (eval/ harness exists; optimiser missing).
- [ ] COMPOSITE RULES via corroboration (supersedes the slop soft-marker).
      Insight: several SIMPLE selectors that fire together are more reliable than
      one big complex regex. A "slop" rule on its own (e.g. an `internal
      hostname:` label, or a bare private-IP pattern, or a money figure) is a
      weak signal; but 2-3 of them CORROBORATING in the same file/block is a
      strong signal of real business-sensitive data. So slop rules are not junk
      — they are BUILDING BLOCKS.
      keur.toml already DESIGNS this but the policy engine does not implement it:
        corroboration-bonus = 0.10   corroboration-cap = 0.99
        escalate-band-low = 0.40     escalate-band-high = 0.75
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
- [ ] SARIF output format — the ecosystem lingua franca (GitHub/GitLab code
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
            * bedrock           — AWS (Claude/Titan/Llama); fits the AWS-sec work
            * kiro              — likely best per-rule quality; use for the HARD
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
      Current violations to address:
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
- [ ] SYSTEM INTEL must follow the adapter/provider pattern (currently macOS-only).
      `keur learn`'s resource readout (RAM free, CPU load, run duration) in
      cmd/learn.cpp uses macOS-only APIs behind `#if defined(__APPLE__)`:
      host_statistics64 / mach + sysctlbyname("vm.loadavg","hw.memsize",
      "hw.logicalcpu"). This BREAKS on Linux and Windows and is raw system I/O
      inside a cmd/ (a KEUR-ARCH-001 smell — I/O belongs in an adapter).
      Extract a SysInfo adapter (adapters/sysinfo.h) with one interface —
      { load1(), ncpu(), ram_total_gb(), ram_free_gb() } — and platform impls:
        * macOS   : mach host_statistics64 + sysctlbyname (current code)
        * Linux   : /proc/loadavg, /proc/meminfo (MemAvailable), nproc
        * Windows : GlobalMemoryStatusEx, GetSystemInfo, perf counters
      cmd/learn.cpp then depends on the adapter, not on the platform. Same
      injectable-seam benefit as the Git/Fs/Proc adapters (ADR-003): tests can
      feed fake resource numbers. Until then the readout only works on macOS.
- [ ] Why do we have multipe Makefiles
- [ ] why is rules apart from keur in src? what isthe pupose?
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
