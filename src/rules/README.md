# src/rules/ — the rule engine (code)

This is the compiled rule **engine**: the C++ that reads `.rule` data files and
evaluates them against source files. The rule **data** itself lives in the
top-level [`rules/`](../../rules/) directory.

```text
src/rules/        <- you are here: the ENGINE (code)
rules/            <- the DATA (.rule files the engine reads)
```

## Files

- `rule_engine.h` / `rule_engine.cpp` — RE2-powered, single-pass, extension
  indexed. Parses `.rule` files, matches patterns, emits normalised
  `RuleFinding`s. Supported engines: `pattern`, `presence`, `absence`.
- `main.cpp` — the standalone `keur-rules` binary (used by the eval harness and
  as a thin CLI over the engine). The main `keur` binary links the engine
  directly (see `src/keur/`).
- `Makefile` — builds `bin/keur-rules`.

See `docs/adr/ADR-001-native-rule-engine.md` for the design rationale (why a
native engine replaced the shell prototype: 5 min to 0.16 s on the corpus).
