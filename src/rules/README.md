# src/rules/ — the rule engine (code)

This is the compiled rule **engine**: the C++ that reads `.rule` data files and
evaluates them against source files. The rule **data** itself lives in the
top-level [`rules/`](../../rules/) directory.

```text
src/rules/        <- you are here: the ENGINE (code)
rules/            <- the DATA (.rule files the engine reads)
```

## Files

- `rule_engine.h` — the public header: `Rule`, `RuleFinding`, `rules_load`,
  `rules_scan_files`, `rules_scan_dir`. The only surface `src/keur/` includes.
- `rule/` — the engine implementation, split by concern:
  `parse` (.rule → `Rule`), `match` (RE2 pattern matching), `scan` (per-file
  dispatch), `block` (brace-scoped presence/absence), `engines` (the engine
  registry — ADR-014), `facts` (ADR-009 fact derivation), `semver`,
  `composite` (leaf AND leaf → sharper finding), `resolve` (difficulty vector →
  active rule set).
- `tokenizer.{h,cpp}` — comment/string-aware tokenisation used by matching.
- `main.cpp` — the standalone `keur-rules` binary (used by the eval harness and
  as a thin CLI over the engine). The main `keur` binary links the engine
  directly (see `src/keur/`).
- `Makefile` — builds `bin/keur-rules`.

Supported engines (data-selected via the `engine:` field, ADR-004/ADR-014):
`pattern`, `presence`, `absence`, `multiline`, `block-absence`, `max-nesting`,
`count`, `extract-duplicates`, `max-lines`, `max-dir-files`, `path-derived`,
`file-presence`, `file-absence`, `composite`.

See `docs/adr/ADR-001-native-rule-engine.md` for the design rationale (why a
native engine replaced the shell prototype: 5 min to 0.16 s on the corpus), and
[`../README.md`](../README.md) for how this engine relates to the keur app.
