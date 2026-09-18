# rules/ — rule data (detection definitions)

This directory holds keur's rule **data**: `.rule` files that define what to
detect. It is not code. The engine that reads and runs these files lives in
[`src/rules/`](../src/rules/) (the compiled RE2 rule engine).

```text
rules/            <- you are here: rule DATA (.rule files)
src/rules/        <- the rule ENGINE (C++: rule_engine.cpp, reads .rule files)
```

## Layout

- `security/` built-in security rules (e.g. KEUR-SEC-001 private key).
- `keur-internal/` keur's own architecture controls that keep the codebase
  honest:
  - `KEUR-ARCH-001..004` — I/O only in adapters, namespace matches directory.
    See `docs/adr/ADR-003-provider-architecture.md`.
  - `KEUR-PORT-001..005` — cross-platform portability: no platform `#ifdef`,
    OS-only header, POSIX-only libc call, hardcoded path separator, or POSIX
    shell command outside the platform layer (`src/keur/common/`). Adapted from
    cpm's STYLE-012..017 / QUAL-078. See `docs/adr/ADR-005-platform-abstraction.md`.
  - `KEUR-PORT-006` — word-size portability (`warn`): no `(int)` truncation of
    `.size()`/`.length()`, no pointer→32-bit cast, no 64-bit-assuming `long`.
    See `docs/adr/ADR-006-word-size-portability.md`.

## Rule format

A `.rule` file is flat YAML-ish. Minimal example:

```yaml
id: MY-RULE-001
title: Short human title
category: secret            # secret | pii | quality | architecture | ...
severity: error             # error | critical | warning | info
engine: pattern             # pattern | presence | absence
action: block               # block | warn | advisory  (optional; derived from severity)
target:
  extensions: .py .js        # space-separated
  filenames: Dockerfile      # optional, match basename
  exclude_paths: test/ vendor/
  content_contains: literal  # optional fast pre-filter on file content
patterns:
  - regex: some_regex_here
    message: What is wrong and why
confidence: 0.9              # optional
fix: How to fix it           # optional
```

Engines:

- `pattern` a match anywhere is a finding (most rules).
- `presence` first match only (reports once).
- `absence` fires when the regex is NOT found (e.g. a required header missing).

## Loading extra rules

The engine loads `$KEUR_HOME/rules` plus every colon-separated entry in
`$KEUR_RULES_DIRS`. Point that at your own directory to add private rules:

```sh
KEUR_RULES_DIRS="$HOME/my-keur-rules" keur scan .
```

Inline suppression on the offending line: `keur:ignore RULE-ID` (or a bare
`keur:ignore` to suppress all rules on that line).
