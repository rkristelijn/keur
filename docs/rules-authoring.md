# Writing keur rules

A keur rule is a small `.rule` file (`key: value`, YAML-ish) under a rules
directory. This is the complete reference for the rule format and every engine
keur supports today. It is the authoritative companion to the ADRs
(ADR-001 native engine, ADR-004 staged engine evolution) and the source for the
rule-authoring model priming (`eval/Modelfile`).

Field names below are exactly the ones the parser reads
(`src/rules/rule/parse.cpp`), so this list stays in sync with the code. Rules
whose engine keur does not support are reported, never silently skipped.

## Anatomy of a rule

```yaml
id: KEUR-SEC-010               # unique, stable ID (never renamed once used)
title: Hardcoded password      # short human title
category: secret               # secret | sast | cmdi | cbi | pii | quality | inclusivity | ...
severity: critical             # critical | error | warning | info
engine: pattern                # pattern | presence | absence | file-absence | file-presence | block-absence
action: block                  # optional: block | warn | advisory (else derived from severity)
confidence: 0.9                # optional: 0.0..1.0 (else derived from action)
message: default finding text  # optional rule-level message (fallback for patterns without one)
fix: Load it from a secret store instead.   # optional remediation hint
skip_comments: true            # optional: strip comments before matching
skip_strings: false            # optional: strip string literals before matching
target:
  extensions: .py .js .env     # space-separated file extensions
  filenames: Dockerfile .env   # space-separated exact basenames (optional)
  exclude_paths: test/ vendor/ # substring path excludes (optional)
  content_contains: password   # fast literal pre-filter: skip files without it (optional)
patterns:
  - regex: (?i)password\s*[:=]\s*["'][^"']{8,}["']
    message: what was found and why it matters
```

Only `id` and at least one pattern (or a file-level engine) are strictly
required. `severity` drives the default `action`/`confidence`:
critical/error → block/0.9, warning → warn/0.6, else advisory/0.4.

## RE2 rules (the regex engine)

keur uses RE2 (linear-time, no catastrophic backtracking). It has **no
backreferences** (`\1`) and **no lookaround** (`(?=...)`, `(?<=...)`). If you
reach for those, restructure. Use character classes and quantifiers
(`[A-Za-z0-9_-]`, `\d`, `\w`, `{n,m}`, `+`, `*`), `(?i)` for case-insensitive,
`^`/`$` for line boundaries. The regex value is written RAW and UNQUOTED.

## Regex craft: tricks that work

- **Word boundaries** `\b` around keywords so `eval` doesn't match `evaluate`
  or `retrieval`: `\beval\s*\(`.
- **Negated class instead of `.`** to stay on one token: `"[^"']*"` matches a
  quoted string body without greedily swallowing the rest of the line. Prefer
  `[^"']` over `.*` inside quotes.
- **Non-greedy** `*?` / `+?` when you must span but want the SHORTEST match:
  `\(.*?\)` for the first parenthesised group.
- **Both separators** for assignments (config vs code): `\s*[:=]\s*`.
- **Length bounds** to dodge ordinary constants: a secret value is usually
  `{12,}` chars; a short literal is noise.
- **Optional separators in identifiers**: `api[_-]?key` matches apikey, api_key,
  api-key.
- **Anchor to structure, not to a value**: match the SHAPE
  (`AKIA[0-9A-Z]{16}`), never the literal.

## Regex anti-patterns (reject these)

- **Memorised literals** — `regex: AKIAIOSFODNN7EXAMPLE` or `regex: 1234`. Finds
  one value, generalises to nothing. keur's optimiser gate rejects these as
  overfit.
- **Bare `.*` / `.+`** as the whole match or between anchors — matches almost
  anything, floods findings. Constrain with a negated class or a keyword anchor.
- **Catastrophic-backtracking SHAPES** — `(.*)*`, `(a+)+`, `(\w+\s*)*`. RE2 is
  linear so it won't hang, but these shapes signal a vague rule that matches
  garbage; rewrite to a precise structure.
- **Unanchored keyword** — `password` (no `\b`, no assignment context) fires on
  every mention in comments/docs. Anchor it: `(?i)\bpassword\s*[:=]`.
- **Unescaped metacharacters** — `.`, `(`, `)`, `[`, `+`, `*`, `?`, `|`, `$`
  meant literally must be escaped (`\.`), or you match far more than intended.
- **Trying lookaround/backrefs** — not supported (see above); restructure or use
  `block-absence` / `exclude` semantics instead.

## Generalise, never memorise

A rule with `regex: AKIAIOSFODNN7EXAMPLE` finds one value and nothing else.
Encode the STRUCTURE: AWS keys are `AKIA[0-9A-Z]{16}`; a generic secret is
`(?i)(api[_-]?key|token|secret|password)\s*[:=]\s*["'][^"']{12,}["']`. Prefer one
broad-but-anchored pattern (keyword-anchored, length-bounded to dodge normal
constants) over a narrow one. Think about false positives before you answer.

## Engines

### `pattern` (default)
Flags each line matching any `patterns[].regex`.

### `presence`
Flags a file where a pattern DOES appear (often gated by `content_contains`).

### `absence`
Flags a file where NO line matches the pattern (something required is missing).

### `file-absence` / `file-presence`
Operate on the file tree, not content — no patterns needed.
`file-absence` flags when no file matches `target.filenames`/`extensions`
(e.g. "project has no SECURITY.md"). `file-presence` flags each matching file
(e.g. "committed .env").

```yaml
id: KEUR-PROJ-001
title: Missing SECURITY.md
category: quality
severity: warning
engine: file-absence
target:
  filenames: SECURITY.md
```

### `block-absence`
Brace-scope analysis without a full parser: find the block opened by the first
pattern, count `{`/`}` to its end, flag the block when a required inner regex
never appears inside it.

- `patterns[0].regex` — the block-START matcher (e.g. `\b(it|test)\s*\(`).
- `block_inner` — regex that MUST appear inside the block's braces.
- `block_trigger` (optional) — gate: only evaluate blocks containing this regex.
  Turns it into a general "scope contains A but not B" check.

Set `skip_comments: true` / `skip_strings: true` so braces inside comments or
strings don't skew the depth count.

```yaml
# a test block with no assertion
id: KEUR-TEST-001
title: Test has no assertion
category: quality
severity: warning
engine: block-absence
skip_strings: true
target:
  extensions: .js .ts
patterns:
  - regex: \b(it|test)\s*\(
block_inner: \b(expect|assert)\s*[.(]
```

## Comment / string awareness

`skip_comments: true` strips comments (per language, line numbers preserved) so
a rule for `system(` does not fire on the word inside a comment.
`skip_strings: true` also blanks string literals. Opt in per rule; rules that
do not opt in match the raw source.

## Suppression

Inline, on the offending line: `keur:ignore` (suppresses all) or
`keur:ignore <rule-id|category>` (token-exact, so `SEC-04` does not suppress
`SEC-043`).

## Rule resolution (cascade)

Rules load general → specific, later winning (like CSS): built-in
(`$KEUR_HOME/rules`) → global (`~/.config/keur/rules`) → project
(`.keur/rules`, found by walking up) → `$KEUR_RULES_DIRS`. Because the format is
cpm-compatible, an existing cpm rule corpus can be reused by pointing
`KEUR_RULES_DIRS` at it. Inspect the resolved set with `keur rules list`.

## Verifying a new rule

1. `cd src/rules && make` — the rule parses and its regex compiles in RE2
   (invalid regex is reported to stderr).
2. `keur scan <path>` — confirm it fires where expected and NOT where it should
   not (write a negative case too).
3. Add a scenario in `src/keur/keur_test.cpp` (see the skip_comments /
   block-absence integration tests) asserting the exact findings.
4. `cd src/keur && make test` — all rule tests green.
