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
- `secrets/` hardcoded-secret detectors.
- `supply-chain/` dependency / SBOM / CI-supply-chain rules.
- `quality/` maintainability norms (e.g. KEUR-QUAL-003..006: `[[` over `[`,
  max nesting depth, max file lines, max dir units).
- `inclusivity/` inclusive-language rules.
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

## Provenance: the `source:` field

Rules keep their origin in a `source:` field instead of a separate directory.
An empty `source:` means keur's own rule. `source: cpm` marks a rule adopted
from cpm (github.com/rkristelijn/cpm, MIT) — keur's `.rule` format is
cpm-compatible, so those load unchanged and keep their cpm IDs (a stable
contract). The field is metadata: the engine parses it and otherwise ignores
it. It exists so imported rules need no separate tree while still supporting
attribution, upstream-sync, and corpus-exclude (grep `source: cpm`). The
`secrets/`, `security/` and `supply-chain/` families are largely cpm-sourced;
`extract-duplicates` and other unsupported engines were filtered out on import.

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
- `block-absence` a brace-scoped block that lacks a required inner match.
- `count` fires once when a pattern matches ≥ `min_count` times.
- `multiline` matches across a sliding window of lines.
- `path-derived` the expected string is computed from the file's path.
- `max-nesting` flags control nesting deeper than `min_count` (S134).
- `max-lines` flags a file over `min_count` net lines.
- `max-dir-files` (file-level) flags a directory over `min_count` logical units.
- `extract-duplicates` flags a capture-group value that repeats within a file.
- `file-absence` / `file-presence` file-level: required/forbidden files.

See `docs/rules-authoring.md` for the full reference.

## Loading extra rules — the dir cascade

The engine loads rules from several directories, general → specific (later dirs
augment earlier ones). A missing dir is silently skipped, so you only create the
layers you need:

1. `$KEUR_HOME/rules` — built-in rules shipped with keur (this directory).
2. `$XDG_CONFIG_HOME/keur/rules` (falls back to `~/.config/keur/rules`) —
   global / company rules, per machine.
3. `~/.keur/rules` — **personal, cross-repo PRIVATE rules** (see below).
4. `<git-root>/.keur/rules` — project rules that travel with the repo.
5. each colon-separated entry in `$KEUR_RULES_DIRS` — explicit override.

```sh
KEUR_RULES_DIRS="$HOME/my-keur-rules" keur scan .   # ad-hoc extra dir
```

### Private BII/PII denylist (`~/.keur/rules`)

Working cross-repo, you do not want **business-identifiable information** — your
username, machine name, internal hostnames, a customer id — leaking into a rule,
a fixture, or any commit. Put a denylist of literal forbidden terms in
`~/.keur/rules/` and keur blocks them in **every** repo automatically:

```yaml
# ~/.keur/rules/BII-denylist.rule   (NEVER committed — lives outside every repo)
id: BII-DENYLIST-001
title: forbidden business-identifiable term
category: pii
severity: critical
engine: pattern
target:
  exclude_paths: .git/
patterns:
  - regex: your-internal-hostname\.example\.local
    message: internal hostname must not be committed — use a placeholder
  - regex: ACME-CUSTOMER-\d+
    message: customer id must not be committed
action: block
```

Why `~/.keur/rules` and not a symlink inside the repo: it lives **outside every
git tree**, so git can never stage it and keur's scan-walker never reads it —
the forbidden terms themselves never risk being committed. If the file is absent
(a fresh machine, CI), the layer is simply skipped — the gate never fails just
because your private list is missing.

> **Note:** for a shared, machine-wide BII/PII denylist, a central vault already
> exists at `~/.local/share/pii/patterns.d/*.pii` (consumed by cpm and the global
> git hook via `$PII_VAULT`). Native `$PII_VAULT` support in keur — so all three
> consumers read one source — is planned separately (see TODO). `~/.keur/rules`
> is the general-purpose personal-rules layer; a one-off private denylist here is
> fine, but prefer the vault once keur reads it directly.

Inline suppression on the offending line: `keur:ignore RULE-ID` (or a bare
`keur:ignore` to suppress all rules on that line).
