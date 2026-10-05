# src/ — keur source layout

keur's C++ source is split into two layers, each with its own `Makefile` and its
own build artifact. This is deliberate: the **rule engine** is a reusable lower
layer, and **keur** is the application built on top of it.

```text
src/
├── rules/     ENGINE — reads .rule data, evaluates it against source files
│              → builds bin/keur-rules (standalone engine binary)
└── keur/      APP    — the CLI, hooks, config, adapters, TUIs
               → builds bin/keur (the product), linking the engine in directly
```

## Why `rules/` is separate from `keur/`

They are different concerns at different layers:

- **`src/rules/`** is the rule engine: RE2-powered parsing + matching of `.rule`
  files into normalised findings. It knows nothing about git, hooks, config
  cascades, or the terminal. It depends only on RE2/abseil and the C++ standard
  library. Its public surface is one header, `rule_engine.h`.
- **`src/keur/`** is the application: subcommand orchestration (`cmd/`), the only
  I/O seams (`adapters/` — git, fs, proc), the config layer, the report provider,
  and the TUIs. It *uses* the engine as a dependency.

The dependency is one-way: `keur/` includes `../rules/rule_engine.h`; `rules/`
never includes anything from `keur/`. That keeps the engine testable and
embeddable on its own, and keeps the app free to evolve its UX without touching
match semantics.

Note the matching top-level pair, which is the **code vs data** axis — a
different distinction from the engine-vs-app axis above:

```text
src/rules/   the engine CODE (C++ that evaluates rules)
rules/       the rule DATA   (.rule files the engine reads; ADR-009)
```

So `src/rules/` (engine code) reads `rules/` (rule data). See
[`rules/README.md`](../rules/README.md) for the data side.

## Why there are two Makefiles

Each layer builds independently:

| Makefile | Builds | Purpose |
|----------|--------|---------|
| `src/rules/Makefile` | `bin/keur-rules` | the engine as a **standalone** binary — a thin CLI over the matcher, used by the eval/learn harness (`eval/*.sh`) to score rules against the corpus without the full app. |
| `src/keur/Makefile` | `bin/keur` | the **product**. It compiles the engine's translation units (`../rules/rule/*.cpp`) straight into `bin/keur` as its lower layer, then adds `cmd/`, `adapters/`, `config/`, `report/`, `render/`, `manager/`, `learn/`. Also owns the full test/coverage/mutation/lint pipeline. |

`bin/keur` does **not** shell out to `bin/keur-rules`; it links the engine code
directly (ADR-002, single binary). The standalone `keur-rules` exists so the
engine can be built and exercised in isolation — the eval harness is its main
consumer.

Build either from its own directory:

```bash
cd src/keur  && make build   # bin/keur   (the product)
cd src/rules && make all      # bin/keur-rules (engine only)
```

See `src/keur/README.md` for the app-layer folder map and `src/rules/README.md`
for the engine internals. Design rationale:
[ADR-001](../docs/adr/ADR-001-native-rule-engine.md) (native engine),
[ADR-002](../docs/adr/ADR-002-single-cpp-binary.md) (single binary),
[ADR-003](../docs/adr/ADR-003-provider-architecture.md) (adapters/providers).
