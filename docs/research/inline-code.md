# Inline code: what counts as inline, and what keur catches today

@id research-inline-code

Research for a family of keur rules against **inline** content: material of
another concern or language embedded in a host source file. Trigger: the loods
spike S-3 (`~/git/hub/loods/spikes/s3/`) was written on a repo that sits at keur
level 5 and still embedded SQL, API endpoints, query parameters and report prose
in C++, and keur reported **none** of it. Researched 2026-11-20; nothing built yet.

## The question

When is a literal in code "inline" (it belongs somewhere else), and when is it just
code? Without a crisp answer, a rule either fires on every string or misses the
cases that matter.

## Working definition

A literal or embedded block is **inline** when at least one of these holds:

1. **Another language.** It has its own grammar and tooling (SQL, shell, Python,
   HTML, CSS, GraphQL, complex regex, JSON or YAML templates). It cannot be
   linted, formatted or tested as what it is while it sits inside a host string.
2. **Another reason to change.** It changes for reasons other than the code around
   it (SRP at the file level): a vendor changes an endpoint, an operator changes a
   timeout, a writer rewords a message, a schema gets a column.
3. **Another environment.** It differs per deployment, tenant or user: hosts,
   ports, paths, account ids, workspace ids, feature flags (12-factor III).
4. **Domain knowledge in a generic engine.** A vendor, provider or product name, or
   a table of them, inside code that should be generic (keur's own
   `KEUR-ARCH-005`; loods `ADR-009` "no provider-specific code paths").

And it is **not** inline when it is part of the host language's own contract:

- protocol constants in the adapter that implements the protocol (`"Accept:
  application/json"` in an HTTP adapter, `SQLITE_OK`);
- identifiers and format specifiers (`"%s"`, small numbers, `true`/`false`);
- test fixtures and expected values in tests;
- log and debug messages meant for developers (a gray zone, see below);
- a single short named constant, when the value has one reason to change and that
  reason is the code itself.

## Taxonomy

| Family | Examples | Why it hurts | Where it belongs |
|--------|----------|--------------|------------------|
| Query languages | SQL DDL and DML, PRAGMA, GraphQL, JMESPath, jq programs | not lintable, schema drift, injection risk when built dynamically | `.sql` / `.graphql` files, embedded at build time or loaded |
| Programs in another language | Python or awk programs in shell, shell in `system()`, CI `run:` blocks, Dockerfile `RUN` chains | one file, two languages; untestable halves | standalone script with its own tests |
| Markup and style | HTML or CSS in server code, inline style props, ANSI escapes | presentation mixed with logic | templates, theme, stylesheet |
| Endpoints and contracts | base URLs, API paths (`/team/{id}/task`), query parameters, headers specific to a vendor | vendor changes force code changes | provider config (catalog) |
| Environment values | hosts, ports, file paths, account ids, workspace ids, regions | differ per deployment | config or environment |
| Tuning values | timeouts, page sizes, retry counts, limits, TTLs | operators tune them, not developers | config with documented defaults |
| Domain data | status-to-enum maps, CVE or version tables, allow and deny lists, field mappings | grows with knowledge, not with code | data files or rules |
| User-facing text | messages, labels, report headings, help text | wording changes; translation; consistency | message catalog or templates |
| Magic values | unnamed numbers and repeated strings | meaning is hidden | named constant (or one of the rows above) |
| Secrets | tokens, keys, passwords | security | vault (already covered by keur) |

## Prior art

| Source | Rule or concept | Takeaway |
|--------|-----------------|----------|
| SonarQube | S1075 "URIs should not be hardcoded", S1192 "string literals should not be duplicated", S109 magic numbers, S2068 hardcoded credentials | Widely used; known false positives on test infrastructure and framework-mandated literals, so tests and protocol constants need exclusions |
| MITRE CWE-547 | Hard-coded, security-relevant constants | Security framing: constants without symbolic names cause mistakes during change |
| OWASP SCWE-008 | Hardcoded constants | Values that may change with external factors should not be embedded |
| clang-tidy | `readability-magic-numbers`, `cppcoreguidelines-avoid-magic-numbers` | Configurable ignore lists (0, 1, powers of two); accepts named constants |
| eslint-plugin-i18next | `no-literal-string` | Flags user-facing literals; Android Lint `HardcodedText` and erb_lint `HardCodedString` do the same per platform |
| 12-factor app | III Config | Anything that varies between deploys is config |
| keur itself | `KEUR-ARCH-005` (no framework or CVE tables in the engine), ADR-009 (rules are data), ADR-058 (literal-frequency scanner) | The principle already exists for keur's own engine; generalise it |

## What keur catches today

Existing rules (rules/quality, rules/security, rules/keur-internal):

| Rule | Covers | Targets | Maturity, action |
|------|--------|---------|------------------|
| `QUAL-030` inline SQL | multi-clause DML, DDL | `.ts .js .tsx .jsx .py .java .kt .cs .go .rb .php .rs` | 4, advisory |
| `MIG-026` raw SQL in ORM | migrations | ORM files | 4 |
| `QUAL-065` hardcoded URL or path | quoted URLs | `.ts .js .jsx .tsx .py .java .kt .cs .go` | 3, advisory |
| `12F-010`, `12F-011`, `GO-QUAL-018`, `IAC-SEC-011` | connection strings, ports, addresses, AWS account ids | per language | 3–4 |
| `SH-QUAL-021` | Python, Node, Ruby, Perl programs in shell | `.sh .bash` | 3, advisory |
| `QUAL-033`, `QUAL-034` | inline shell in CI and Dockerfiles | CI, Docker | 3–4 |
| `QUAL-032` | inline HTML in server code | server languages | 4 |
| `QUAL-031` | complex inline regex | — | 4 |
| `UI-MIS-0xx`, `BUILD-010`, `CPM-INT-006` | inline style, hardcoded colours, ANSI escapes | framework specific | 3–4 |
| `STYLE-050`, `QUAL-012`, `TEST-033` | magic numbers | — | 3–4 |
| `QUAL-071` | repeated magic status strings | JS and TS only | 3, advisory |
| `KEUR-ARCH-005` | framework or CVE tables in keur's engine | keur's own `src/` | 3 |
| `literal-classes` + ADR-058 pipeline | URL, hostname, long numbers, ARNs as "identity" literals | all, via tokenizer | generic, report only |

### Measured on the loods spike

`keur scan spikes` on loods (level 5, all 1379 rules active):

- The spike contains **41 inline items**: 4 endpoints (base URL and paths), 6 query
  parameters, 2 vendor auth details (header format, secret name), 5 field-mapping
  keys (`user.id`, `teams[0].id`, …), 3 provider or query names in engine code,
  11 SQL statements and pragmas, 1 tuning value (`CURLOPT_TIMEOUT, 30L`) and 9
  lines of report prose. Every one of these is already expressed as data in the
  kooiaapi `provider.toml` the spike was meant to follow.
- keur reported **0 inline findings** for them. It did report platform-portability
  blocks (`KEUR-PORT-002`, `KEUR-PORT-003`: POSIX calls outside a platform layer),
  which are correct but a different topic.

### Why they were missed

1. **Language targets.** `QUAL-030` and `QUAL-065` do not include `.c .cpp .cc .h
   .hpp .swift .sh`. C and C++ — keur's own and loods' language — are not covered.
2. **Pattern shape.** `QUAL-030` needs a multi-clause statement or DDL on one line;
   a C++ raw string (`R"SQL(` … `)SQL"`) spreads DDL over lines, and single-clause
   `INSERT INTO t(a) VALUES (?)` is not matched at all.
3. **No rule exists** for endpoint paths and query strings, for tuning values in
   calls (`CURLOPT_TIMEOUT, 30L`), or for user-facing prose in `printf`, `cout` or
   `std::cerr`.
4. **Actions stay advisory at level 5.** ADR-026 keeps maturity ("is it active")
   and action ("how hard does it fail") orthogonal, so `QUAL-030` is active at
   level 5 and still only advisory. A repo at level 5 therefore cannot express
   "inline is not allowed here" without per-rule overrides.

## Detection: what a pattern can and cannot do

| Can do reliably (pattern or multiline engine) | Needs a heuristic or a human |
|-----------------------------------------------|------------------------------|
| SQL keywords inside string literals, including raw strings | Is a string user-facing or a debug log? |
| Raw-string delimiters that name a language (`R"SQL(`, `<<'PY'`) | Is a number a tuning value or a protocol constant? |
| Quoted `https://` URLs and `/path/{var}` endpoint shapes in source | Is a vendor name in code "domain knowledge" or a legitimate adapter name? |
| `printf`/`cout`/`cerr` with long prose literals | Is a short repeated string an enum value or a coincidence? |
| Known option-setting calls with literal values (`CURLOPT_TIMEOUT`, `setTimeout`) | — |

The heuristic cases are handled best by **path scope**: what is allowed in
`src/adapters/` (protocol constants) is not allowed in `src/common/` or the engine,
and nothing domain-specific is allowed outside the catalog or config tree. keur
already has path-scoped config (ADR-043) and path-derived checks (ADR-020).

## Proposal (for decision, not built)

New or changed rules, all pattern or multiline engine, self-explaining (ADR-035):

| Proposed id | What | Maturity, action | Notes |
|-------------|------|------------------|-------|
| extend `QUAL-030` | add `.c .cpp .cc .h .hpp .swift .sh`; match single-clause DML and `PRAGMA` inside string literals | 3, warn | keep the advisory variant for non-SQL-heavy languages if needed |
| `KEUR-INL-001` embedded language block | raw strings or heredocs whose delimiter or first token names a language (SQL, PY, JSON, YAML, HTML) | 3, warn | the C++ raw-string case from the spike |
| extend `QUAL-065` | add C, C++, Rust, shell, Ruby, PHP; exclude `tests/` and `vendor/` | 3, warn | |
| `KEUR-INL-002` endpoint in code | string literals shaped like API paths with placeholders or query strings (`"/team/"`, `"?a=…&b="`) outside a config tree | 4, warn | |
| `KEUR-INL-003` tuning literal | literal values passed to known option-setters (timeouts, retries, page sizes) | 4, warn | list of setter names is data in the rule |
| `KEUR-INL-004` user-facing prose | `printf`, `cout`, `cerr`, `puts`, `echo` with a literal of more than about five words, outside a messages or templates module | 5, warn | gray zone; level 5 only |
| `KEUR-INL-005` domain name in generic engine | configured list of provider or vendor names found in configured engine paths | 4, block | generalises `KEUR-ARCH-005`; the list and paths come from the repo's `keur.toml` |

Policy change to decide on, separate from the rules:

- **Maturity escalation.** Let a repo say "from my level, advisory becomes warn and
  warn becomes block" for a family (for example `escalate = ["inline"]` at level 5),
  instead of overriding rules one by one. This touches ADR-026 and needs its own ADR.

Each new rule ships with positive and negative fixtures; the loods spike files are
the first positive corpus, cpp-template and tuigram the first negative corpus.

## Follow-ups (2026-11-21)

- Done: `QUAL-030` rewritten and `QUAL-030b` added (multiline). Fixtures in
  `tests/fixtures/inline-sql/`: 13 of 13 positives, 0 of 3 negative files. Calibration:
  kooiaapi 10 true positives, tooling 39, keur `src/` 0 after excluding `_generated.`.
- Engine: the multiline engine reports the window's start line, which can be a
  comment line before the literal's opener (seen in kooiaapi `migrate.go`). Report
  the line of the match start instead, so `keur:ignore` sits where it reads naturally.
- Manager catalogue: regenerate `catalog_generated.cpp` so `QUAL-030b` shows in
  `keur manager` (matching already works, rules are data).
- New rule candidate: an `else if` chain comparing the same variable with 4 or more
  literals → lookup table (C++ cannot `switch` on strings). First target: keur's own
  `src/rules/rule/parse.cpp`, where the rule-file keys are a schema (ADR-073
  category 2) living in code.

## Open questions

The category model, the reference data and the locale rules that grew out of this
research are decided in `docs/adr/ADR-073-text-vocabulary-formats-reference-data.md`.

1. Should user-facing prose (`KEUR-INL-004`) block at level 5, or is it warn-only
   because a CLI legitimately prints text?
2. Is maturity escalation per family the right policy knob, or should these rules
   simply be `warn` with `maturity: 3`?
3. Where do C++ projects keep embedded SQL: build-time embedding (generated header
   from `.sql` files) or runtime loading? The rule should point at the family
   convention once it is chosen (cpp-template is the place to record it).
4. Do we want a rule-level "allowed paths" convention (for example `adapters/` may
   hold protocol constants), or is per-line `keur:ignore` with a reason enough?
