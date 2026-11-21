# ADR-074 — Diagnostics and messages as data: codes in code, text in a catalog, rendering in an adapter

@id ADR-074
**Status**: proposed · **Date**: 2026-11-21 · **Coverage**: none · **Relates to**: ADR-035 (self-explaining findings), ADR-073 (categories 2, 5 and 9), ADR-022 (cross-reference engine), ADR-026 (maturity levels), `docs/research/inline-code.md`

## Context

`docs/research/inline-code.md` counted nine lines of report prose in the loods
spike S-3 (`printf("store write %8.1f ms (encrypted)\n", …)`) and error text built
inline (`fprintf(stderr, "embed: %s: path must be …")`). ADR-073 put human text in
category 5 and codes in category 2, but did not say **how** a program should emit
an error, a warning or a status line so that the text is not in the code. That is
the question here, because those are the most frequent human-text literals in a CLI.

The pattern is well established:

| Source | Shape |
|--------|-------|
| Clang/LLVM | all diagnostics defined centrally (`Diagnostic*Kinds.td`: `err_…`, `warn_…`, `note_…`), generated into enums; code calls `Diag(loc, diag::err_x) << arg`; a `DiagnosticConsumer` renders |
| Rust | stable error codes (`E0382`) with long explanations via `rustc --explain` |
| Terraform | diagnostics with severity, summary, detail and subject |
| PostgreSQL | SQLSTATE codes separate from message text |
| RFC 9457 Problem Details | stable `type`, plus `title`, `detail`, `instance` |
| IBM message ids | `ABC1234E`: component, number, severity letter |
| Serilog message templates, OpenTelemetry events | the template or event name *is* the identifier; parameters are fields |
| Severity vocabularies | SARIF (`error`, `warning`, `note`), LSP (`Error`, `Warning`, `Information`, `Hint`), syslog RFC 5424 |
| Exit codes | `sysexits.h` (64 usage, 65 data, 74 I/O, 78 config) |

keur already follows this shape for its own findings (rule id, severity, message,
fix — ADR-035). This ADR makes it the expectation for the code keur checks, and
defines the rules.

## Decision

### 1. The model: three separate concerns

1. **Emit (domain code).** Code creates a *diagnostic*: a stable code plus named
   parameters (and an optional location). No text.
2. **Render (presentation adapter).** A reporter looks the code up in the message
   catalog for the user's language and renders it as terminal text, JSON, SARIF or
   JUnit. Long explanations come from the same catalog (`<tool> explain <code>`).
3. **Deliver (sink adapter).** stderr, the log, a notifier, an API response. Logs
   store code plus parameters, never the rendered sentence (ADR-073 category 9).

### 2. Codes

- Shape `<AREA>-<NNN>` (for example `CAT-002`, `SEC-005`), stable forever: a code is
  never reused for another meaning; a retired code stays reserved.
- Severity is a property of the code in the catalog, from a closed set:
  `error | warning | note | info | hint`, mapped to SARIF and LSP levels.
- An optional exit code per error, from `sysexits.h` where one fits.

### 3. The catalog

One catalog per language, one file per kind, all embedded at build time (for C++
through a generated header; other languages load or bundle the files):

```toml
# messages/en/diagnostics.toml
[CAT-002]
severity = "error"
title    = "Unknown key in provider config"
text     = "{file}: unknown key '{key}'"
fix      = "Check the spelling; valid keys: {valid}."
explain  = "docs/explain/CAT-002.md"
exit     = 78
```

Other kinds use the same mechanism with fewer fields: `labels.toml` (report
headings, column names), `help.toml` (usage text). `{name}` is a runtime parameter;
`${term}` references another catalog value (ADR-073 section 1). A generator turns
the catalog into typed emitters, so a misspelled code or a missing parameter is a
compile error in C++.

### 4. What keur checks

| Proposed id | Check | Engine | Maturity, action |
|-------------|-------|--------|------------------|
| `KEUR-TXT-001` | prose literal (several words) passed to an output call — `printf`, `fprintf`, `puts`, `std::cout`, `std::cerr`, `console.log`, `print(`, shell `echo` / `printf` — outside the reporter and catalog paths | pattern | 3 warn, 5 block (with maturity escalation, otherwise warn) |
| `KEUR-TXT-006` | error raised with literal prose instead of a code: `throw std::runtime_error("…")`, `raise ValueError("…")`, `die "…"`, `return errors.New("…")` | pattern | 4 warn |
| `KEUR-TXT-007` | code used in source but missing from the catalog, or defined in the catalog and never used | cross-reference | 4 warn |
| `KEUR-TXT-008` | the same code defined twice, or a code that does not match `<AREA>-<NNN>` | cross-reference, pattern | 3 block |
| `KEUR-TXT-009` | severity outside the closed set | pattern on catalog files | 3 block |
| `KEUR-TXT-002`/`003` (ADR-073) | catalog complete across languages; placeholders identical | cross-reference | 4 |
| `KEUR-VOC-002` (ADR-073) | log call without an event code | pattern | 5 warn |

Allowed and excluded: the reporter module and catalog files themselves (they are
where text belongs), `tests/`, generated code, and single-word or format-only
literals (`"%s\n"`, `"--json"`). The reporter paths are configurable per repo, so a
project names its own reporter directory.

### 5. Maturity

| Level | Expectation |
|-------|-------------|
| 1–2 | none beyond existing rules |
| 3 | prose in output calls warned; codes well-formed and unique; closed severity set |
| 4 | errors carry codes; catalog and source agree; translations consistent |
| 5 | no prose literal in output calls at all; logs carry event codes; `explain` exists for every error |

### 6. keur itself

keur's own CLI still prints prose directly in places (`keur scan: no rules found …`).
keur adopts this pattern for its own messages as a follow-up; until then the new
rules run on keur at warn, so the gap is visible rather than hidden.

## Consequences

- Error and warning text becomes reviewable, translatable and searchable by code;
  logs and AI agents work with codes instead of sentences.
- Adding a message costs one catalog entry plus a call; at level 5 that is the
  point, at lower levels the rule only warns.
- The cross-reference rules (`TXT-007`) wait for ADR-022; the pattern rules
  (`TXT-001`, `TXT-006`, `TXT-008`, `TXT-009`) can ship first.
- False positives are likely where a literal is not prose (format strings, flag
  names); the multi-word threshold and per-repo reporter paths are the levers, and
  fixtures from keur, tooling and loods calibrate them.

## Alternatives considered

- **gettext-style `_("text")` wrappers** — makes text translatable but keeps the
  sentence as the identifier; codes, severity and fixes stay unstructured.
- **Named string constants in code** (`kErrUnknownKey = "…"`) — removes duplication
  but keeps text in code and gives no severity, fix or translation.
- **Free-text logs** — searchable only by wording that changes.

## Open questions

1. Word threshold for `TXT-001`: three or more words, or any literal with a space?
2. Should keur ship a reference reporter and generator for the C++ family (in
   `cpp-template`), so projects do not each write their own?
