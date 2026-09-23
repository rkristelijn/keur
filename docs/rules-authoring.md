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
engine: pattern                # pattern | presence | absence | file-absence | file-presence | block-absence | count | multiline | path-derived
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

### `count`
Flag a file ONCE when the first pattern matches at least `min_count` times. A
reusable "too many of X" primitive: long dispatch chains, too many imports, too
many TODOs. The finding points at the line of the match that crosses the
threshold. Fires once per file, not per match.

- `patterns[0].regex` — what to count.
- `min_count` — the threshold (default 2). Fire only at N or more matches.
- `within` — scope; `file` for now (per-block reserved).

```yaml
# 8+ else-if(x == …) branches → consider a table or strategy registry (ADR-014)
id: KEUR-QUAL-001
title: Long conditional dispatch chain
category: quality
severity: warning
engine: count
min_count: 8
skip_comments: true
target:
  extensions: .cpp .cc .h
patterns:
  - regex: \belse if\s*\([^)]*==
```

### `multiline`
Match a pattern that SPANS lines, bounded to a sliding window. RE2 matches one
line at a time; this engine joins a window of `window` lines with newlines and
matches once over the join, so `[\s\S]*?` / `(?s).` can cross lines **without** a
file-wide match (which would flood findings — see the anti-patterns above). The
finding is reported at the window's START line, so it points at a real place and
inline `keur:ignore` still works. Use it for "assign here, use there" shapes: a
secret assigned on one line and passed to a client a couple of lines down, a SQL
string built over several `+=`, a config value split across lines.

- `patterns[0].regex` — the cross-line pattern (use `[\s\S]*?` to span, kept
  non-greedy; the window bounds it so this cannot run away).
- `within: window` — selects the sliding-window scope.
- `window` — window size in lines (default 2). The match never spans more than
  this many lines; after a hit the scan resumes past the window (one span, one
  finding).

```yaml
# a secret assigned then passed to a client within 3 lines
id: KEUR-SEC-009
title: Hardcoded secret assigned then used
category: secret
severity: warning
engine: multiline
within: window
window: 3
skip_comments: true
target:
  extensions: .py .js .ts .go
patterns:
  - regex: (?i)(secret|token|api[_-]?key)\s*=\s*["'][^"']{12,}["'][\s\S]*?(client|connect|use)\s*\(
```

### `path-derived`
Flag a file when a string **computed from its own path** is absent. The
expectation is not a constant in the rule — it is derived per file, so ONE rule
covers a whole family (every layer, every module) with no per-case edit. This is
the engine behind the layer↔namespace self-check (ADR-020): "a file in
`src/keur/<layer>/` must declare `namespace keur::<layer>`" is one rule, and a
brand-new layer is checked automatically.

- `path_capture` — a regex with capture group(s) applied to the file's path. If
  it does not match, the rule is out of scope for that file (no finding). RE2
  linear, no backreferences.
- `derived_template` — the expected string, with `\1`..`\4` substituted from the
  captured groups. Matched as literal text (ReDoS-free).
- `message` — the finding message (rule-level; no `patterns:` block needed).

Present-or-flag: if the derived string appears on any line, pass; else flag at
line 1. Because the derived string is matched literally, this suits identifier
conventions (namespace/package/export must match the directory or filename).
Note it needs the relevant path segment to be *in the scanned path*, so run it
repo-wide (`keur scan src/keur`), not scoped inside the layer dir.

```yaml
# every file in src/keur/<layer>/ must declare namespace keur::<layer> (ADR-003)
id: KEUR-ARCH-002
title: Layer directory must match its C++ namespace
category: architecture
severity: error
engine: path-derived
action: block
target:
  extensions: .cpp .h
  exclude_paths: common/ util/ main.cpp _test.cpp test/ tests/ vendor/
path_capture: '([a-z_]+)/[a-z0-9_]+\.(cpp|h)$'
derived_template: 'namespace keur::\1'
message: "File under src/keur/<layer>/ must declare 'namespace keur::<layer>' (ADR-003)."
```

Other uses of the same shape: filename must equal an exported symbol,
`package x.y.z` must match the directory path (Java/Go), a test file must sit
beside its source.

### `max-nesting`
Flag a function whose CONTROL-FLOW nesting depth exceeds `min_count` (default 3,
matching SonarQube S134). It reuses the same string/regex-aware brace lexer as
`block-absence`, so braces inside strings, char-literals and regex literals do
not skew the depth. Only braces opened by a control keyword
(`if`/`else`/`for`/`while`/`switch`/`do`) raise the control depth — a
struct/lambda/initializer/plain-block brace balances the raw scope but does not
count. Fires once per over-deep function, at the function's opening line.

- `min_count` — the maximum allowed control-nesting depth (default 3). A
  function whose peak depth is `> min_count` is flagged.
- `patterns[0].regex` — unused for matching (the depth is structural); keep a
  short marker regex so the rule parses and to carry the `message`.

```yaml
# >3 nested control blocks → refactor (guard clauses, extract a helper). S134.
id: KEUR-QUAL-004
title: Function nested more than 3 control blocks deep
category: quality
severity: warning
engine: max-nesting
min_count: 3
target:
  extensions: .c .cpp .cc .h .hpp
patterns:
  - regex: nesting too deep
```

### `max-lines`
Flag a file whose NET line count exceeds `min_count` (default 300). With
`skip_comments: true` the scanner strips comment lines to blanks first, so blank
and comment lines are not counted — the same "net lines" notion as a
`grep -vcE '^\s*(//|/*|*|$)'` lint. Fires once per over-long file, at line 1. A
file-size metric is a per-file property, so it needs no lexer or cross-file
state; the directory-file-count sibling check is a cross-file aggregation and
stays outside the per-file engine.

- `min_count` — the maximum allowed net lines (default 300). A file with more is
  flagged.
- `skip_comments: true` — recommended, so comments/blank lines do not inflate the
  count.
- `patterns[0].regex` — unused for matching (the size is structural); keep a
  short marker regex so the rule parses and carries the `message`.

```yaml
# files well under ~300 net lines — split by responsibility (AGENTS.md).
id: KEUR-QUAL-005
title: Source file over ~300 net lines
category: quality
severity: warning
engine: max-lines
skip_comments: true
min_count: 300
target:
  extensions: .c .cpp .cc .h .hpp
patterns:
  - regex: file too long
```

### `max-dir-files`
A FILE-LEVEL engine (like `file-absence`/`file-presence`): it runs once over the
whole fileset, groups matched files by their parent directory, counts logical
units — a `.h` + `.cpp` with the same stem is ONE unit — and flags a directory
holding more than `min_count` units. This is a cross-file aggregation, which the
per-line engines cannot do but the file-level layer can (it sees every file).
Fires once per over-full directory (the finding's path is the directory).

- `min_count` — the maximum allowed units per directory (default 10).
- `target.extensions` — which files count (scope it to source dirs).
- `patterns[0].regex` — unused for matching (the count is structural); keep a
  short marker regex to carry the `message`.

```yaml
# no source dir > ~10 logical units — regroup into subdirs (AGENTS.md).
id: KEUR-QUAL-006
title: Directory holds more than ~10 logical units
category: quality
severity: warning
engine: max-dir-files
min_count: 10
target:
  extensions: .c .cpp .cc .h .hpp
patterns:
  - regex: too many units
```

### `extract-duplicates`
Per file, apply the `extract` regex (which must contain a capture group), collect
the captured value from each line, and flag the SECOND and later occurrence of the
same value — an in-file duplicate. A seen-set makes it O(n). Unique values never
fire. Use `{match}` in the message to interpolate the duplicated value.

- `extract` — a regex with a capture group (e.g. `it\(\s*['"]([^'"]+)['"]`).
- `capture` — which group holds the value (default 1).
- `patterns[0].message` — supports `{match}` → the duplicated value.

```yaml
# duplicate test name in one file (it('x') / test('x') twice).
id: TEST-038
title: Duplicate test name in same file
category: quality
severity: warning
engine: extract-duplicates
extract: (?:it|test)\(\s*['"]([^'"]+)['"]
capture: 1
patterns:
  - regex: x
    message: "Duplicate test name '{match}' — give each test a unique name"
```

## Facts, `requires:` and composite rules (ADR-009)

Beyond single patterns, a rule can reason about *context* and *combine* other
rules. This is data, not engine logic — the engine only evaluates it.

### Facts

Before matching, keur derives a small, flat, language-neutral **facts** map from
direct manifest pins and file presence only (no dependency graph, no network):

```json
{ "langs": ["js"], "frameworks": ["express"], "versions": {"express":"4.17.1"},
  "runtimes": {"node":"18"} }
```

Sources read: file extensions (langs), `package.json` deps + engines, `go.mod`
require/`go` directive, `pom.xml`, `composer.json`, and Dockerfile `FROM`.
Inspect them with `keur-rules --dir <path> --facts`.

### `requires:` — gate a rule on facts

A rule (single or composite) may declare a `requires:` guard. If the facts don't
satisfy it, the rule is **never loaded, compiled, or run** (most-discriminating-
first: a JS-only repo never pays for Java/Docker rules). Empty guard = always
applies (fully backwards compatible).

```yaml
requires:
  lang: ts go            # any-of: at least one of these languages present
  framework: express     # all-of: each framework must be present
  version: express <4.18.0   # "name <constraint>": <, <=, >, >=, =, or "A - B"
```

Version predicates compare against a **direct pin** only (numeric, so 1.9 < 1.10;
pre-release suffixes ignored). A missing pin means "not satisfied" — keur prefers
a missed conditional finding over a guessed one.

### `engine: composite` — combine other rules with `all`/`any`/`not`

A composite composes the results of ordinary rules (each `ref` also runs
standalone). It runs *after* the normal pass, over the ids that fired, and adds
no new match primitive.

```yaml
id: KEUR-IAC-010
title: Unhardened container image
category: iac
severity: warning
engine: composite
requires:
  framework: docker         # only in repos with a Dockerfile
composite:
  all: KEUR-IAC-002 KEUR-IAC-003   # both must fire
  # any: RULE-A RULE-B             # at least one
  # not: RULE-C                    # must NOT fire
message: "Runs as root AND has no HEALTHCHECK — either alone is a warning, both is a gap."
```

`all`/`any`/`not` take space-separated rule ids; `ref: <id>` is a single-leaf
form. Each referenced id must resolve to a real rule.

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
