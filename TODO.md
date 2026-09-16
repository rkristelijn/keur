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

- [~] output consistent / json / compact / verbose / colour — the report
      PROVIDER supports all modes; still to wire: a `--format` / `--color` flag
      through `keur scan` and `keur check` so users can pick the mode.
- [~] cleanup root folders — src/keur is clean; still to remove: empty dirs
      (test/ optimiser/ policy/), deprecated shell (lib/ scanners/ hooks/),
      and decide on config/ -> .config/. Clarify rules/ (data) vs src/rules/ (engine).

## Open

### Features
- [ ] libmode: run external scanners (semgrep, gitleaks, trivy, ...) via the
      Proc adapter, OR ruleengine mode, or both — normalise into one findings
      report. (Proc adapter seam is ready.)
- [ ] ollama learn/loop as designed (eval/ harness exists; optimiser missing).
- [ ] pluggable private rules: loadable (KEUR_RULES_DIRS works) + docs + man page
      in the binary, so an AI/agent can discover how to add custom rules.

### Docs (independent track — can run in parallel via subagent)
- [ ] README with basic user stuff
- [ ] CONTRIBUTING with links to the docs
- [ ] docs/c4.md with C4 diagrams
- [ ] dataflow diagrams
- [ ] class diagram + ability to regenerate the 'model' split from the view
- [ ] sequence diagrams for the complex flows

### Quality / research
- [ ] rules: do they follow an ISO *ilities structure? would that be applicable?
- [ ] run cpm in extreme mode here + semgrep — dead code, dead docs, links
      between code and docs; all should be green.
- [ ] TUI / a11y / design: are there TUI designs, are we accessible? (see cpm
      knowledge). Tie into the report provider's output modes.

## Next up (recommended order)

1. Finish output track: expose `--format jsonl|compact|verbose` and
   `--color auto|always|never` on scan + check (small, high-value, provider ready).
2. Root cleanup: delete empty/dead dirs, remove deprecated shell, document
   rules/ vs src/rules/. Then run cpm extreme + semgrep green.
3. libmode: external-scanner adapters over Proc, unified report.
4. Docs track (parallel, subagent): README + CONTRIBUTING + C4/diagrams.
5. pluggable rules docs + man page.
6. ollama optimiser loop.

- [ ] Why do we have multipe Makefiles
- [ ] why is rules apart from keur in src? what isthe pupose?
- [ ] 
