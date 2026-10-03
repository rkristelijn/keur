================================================================================
KEUR — SNAPSHOT for /clear  ·  2026-09-27
NEXT TASK: implement `keur learn` -> `keur train` split (ADR-036)
================================================================================

## THE TASK (per ADR-036, already written — read it first)

Read `docs/adr/ADR-036-train-vs-learn-rule-explainers.md`. Implement it:

1. RENAME the optimise-loop command `keur learn` -> `keur train`
   (same flags: --stop --status --source --model --refresh).
2. REPURPOSE `keur learn <RULE-ID>`: print that rule's explainer markdown
   from `docs/rules/<RULE-ID>.md`. Resolution: local ($KEUR_HOME/docs/rules/)
   -> cache ($XDG_CACHE_HOME/keur/rules/) -> remote ONLY on --fetch /
   KEUR_LEARN_FETCH=1 (never auto, never upload). Fallback when no md exists:
   print title/message/fix/CWE from the .rule file (always useful).
3. `keur train` GENERATES the explainer md when the gate ACCEPTS a rule
   (one extra Ollama prompt per accepted rule; reviewed:false front-matter).
   Plus `keur train --explain <RULE-ID>` to back-fill an existing rule.

## DEPRECATION (don't break the running nightly loop)

Keep `keur learn` as a DEPRECATED ALIAS for `keur train` for one release
(print a note pointing at `keur train`), THEN repurpose it to `learn <RULE>`.
kiro-seed just made `keur learn` the nightly default (seed-iterate + authored
GT) — the alias must keep supervise.sh / their nightly run working.

## CODE TOUCH-POINTS (verified 2026-09-27)

- src/keur/main.cpp:69  help text "learn [--stop|--status] Spin the optimise loop"
- src/keur/main.cpp:94  dispatch: {"learn", ... keur::cmd::learn(learn_parse(a))}
                        -> add {"train", ...}; keep "learn" as alias (deprec note)
- src/keur/cmd/learn.cpp / .h  -> SPLIT into:
    cmd/train.cpp (.h)  = the loop (move current learn() here)
    cmd/learn.cpp (.h)  = new: learn(<RULE-ID>) explainer fetch/print
  learn_parse already loops args (line 33) — add a positional <RULE-ID> for learn.
- src/keur/Makefile  -> register cmd/train.cpp (explicit per-object rule, like
  the others — NOTE: Makefile lists sources AND has explicit .o rules, it does
  NOT glob. See how config/keurignore.cpp was added this session for the pattern.)
- Docs/scripts that say "keur learn" for the loop (update to "keur train"):
    eval/optimise.sh, eval/supervise.sh, docs/design/learn-loop-bottlenecks.md,
    docs/adr/ADR-004, ADR-027, ADR-029, README.md
    (ADR-036 itself already uses the new names.)
- src/keur/cmd/learn.cpp learn.cpp default (kiro-seed's): KEUR_LOOP_CMD =
  'optimise.sh seed-iterate 500', KEUR_GT_FILE -> authored-groundtruth.jsonl,
  and it MUST run with KEUR_MATCH_GRANULARITY=category (file gives recall 0.000).
  Preserve all of that when moving to cmd/train.cpp.

## NEW FILES TO ADD

- docs/rules/_TEMPLATE.md  (explainer template — see ADR-036 §5 content contract)
- docs/rules/<RULE-ID>.md   for the session's new rules to seed the feature:
    KEUR-CMDI-003, KEUR-NOSQLI-001, KEUR-XSS-001, KEUR-INFO-001, KEUR-NEXT-001
- Add BLOCK-output line "learn more: keur learn <RULE>" to the ADR-035 formatter
  in src/keur/cmd/check.cpp format_finding_line().

## VERIFY AFTER (non-negotiable, AGENTS.md)

  cd src/rules && make && cd ../keur && make test   # must be green (12 suites)
  eval/doc-coverage.sh                              # no dead links
  ./bin/keur train --status                         # loop cmd still works
  ./bin/keur learn KEUR-CMDI-003                     # prints explainer/fallback
  ./bin/keur learn                                   # deprec alias -> train (with note)

## REPO STATE RIGHT NOW (all committed, tree clean)

Branch: feat/manager-path-derived (shared with kiro-seed). NOT pushed by me.
My 4 commits on top of kiro-seed's 4:
  83f7140 feat(rules): NestJS + session rules
  7ae67e5 feat(rules): authored-GT rules + SEC-038 fix (dead on JS/TS -> 727 hits)
  1d80a8c feat(eval): authored-GT pipeline + benchmark (ADR-030/031/032)
  3253970 feat(check): self-explaining findings + .keurignore (ADR-035)

## KEY CONTEXT (so a fresh session doesn't re-derive it)

- keur = fast RE2 pre-commit gate; reframed (ADR-035) as an AI FEEDBACK LOOP:
  high recall + speed matter, FP mitigated by self-explaining errors + easy
  suppression (AI reads the finding, fixes or suppresses — not --no-verify).
- Benchmark (eval/benchmark.sh): keur 39% recall / 33s vs semgrep 9% / 855s.
  keur wins recall+speed, loses precision (noisier). That's the accepted trade.
- Ground truth: eval/corpus/authored-groundtruth.jsonl = the repos' OWN vuln
  markers (harvested by eval/gt-harvest.sh), NOT tool output. reviewed:true =
  adjudicated. measure/train against it with KEUR_MATCH_GRANULARITY=category.
- 3 suppression layers (ADR-035): inline keur:ignore <RULE> · .keurignore file
  (secrets by VALUE, code by location; secrets NEVER line-suppressable) ·
  keur.toml [rules] disabled.
- Coordinate with sibling agent kiro-seed via .agent-mailbox/messages.jsonl.
  They own the nightly train loop + NEST/PROM/JS/TS rules + mestjs repo.
  ADR-033/034 are theirs; 030/031/032/035/036 are mine.
- KNOWN ENGINE BUG (open): skip_comments:true blanks matched code on .ts/.js
  (killed SEC-038; workaround = remove skip_comments). Real fix + audit of all
  `grep -rl "skip_comments: true" rules/` is a separate TODO.

================================================================================
Full history: handover_2026-09-26.log (previous session).
================================================================================
