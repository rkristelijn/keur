# ADR-073 — Text, vocabulary and formats: nine categories, normalised reference data, and what keur checks

@id ADR-073
**Status**: proposed · **Date**: 2026-11-21 · **Coverage**: none · **Relates to**: ADR-009 (rules are data), ADR-022 (cross-reference engine), ADR-026 (rule maturity levels), ADR-035 (self-explaining findings), ADR-058 (literal-frequency scanner), ADR-061 (doc standards by CMMI), `docs/research/inline-code.md`

## Context

`docs/research/inline-code.md` showed that a repo at level 5 could still embed SQL,
endpoints, tuning values and user-facing prose in C++ without a single finding. The
follow-up discussion (2026-11-21) widened the question from "inline" to "what kinds
of non-code content exist, which of them are human-facing, and which standard
defines each one". Two conclusions:

1. **"i18n" is not "language".** Internationalisation (i18n) is the engineering that
   makes code adaptable without changes. Localisation (l10n) is the adaptation per
   locale and covers both translation (t9n) and value formatting (dates, time zones,
   numbers, currency, units). Domain vocabulary (states, events, error codes) is not
   i18n at all; it is an identifier contract that is never translated.
2. **A check needs a reference.** keur cannot say "`en-UK` is wrong" or "this
   currency code does not exist" without the official lists. Those lists exist
   (ISO, IANA, Unicode CLDR) and must be shipped as versioned data, the same way
   rules are data (ADR-009).

## Decision

### 1. Nine categories of non-code content

| # | Category | Translated? | Defined by | Belongs in |
|---|----------|-------------|-----------|------------|
| 1 | Code (logic) | — | — | source files |
| 2 | Domain vocabulary: states, events, kinds, error codes, message ids | never | DDD ubiquitous language; CloudEvents, AsyncAPI (events); RFC 9457 Problem Details `type` (errors); OpenTelemetry semantic conventions (event and attribute names) | a schema as data, generated into enums |
| 3 | Business rules and decision data: thresholds, mappings, decision tables | never | OMG DMN; rule engines | rule and data files |
| 4 | Configuration: hosts, paths, endpoints, timeouts, secret names | never | 12-factor app, factor III | config |
| 5 | Human text: labels, user-facing errors, help, report headings | yes (t9n) | gettext, ICU MessageFormat, Project Fluent; POSIX `LC_MESSAGES` | a message catalog per language |
| 6 | Locale formatting: date and time, time zone, numbers, currency, units, sorting, week start | yes (l10n) | Unicode CLDR and UTS #35 (LDML); BCP 47 `-u-` keys; POSIX `LC_TIME`, `LC_NUMERIC`, `LC_MONETARY`, `LC_COLLATE`, `LC_MEASUREMENT` | canonical storage, formatter at display |
| 7 | Embedded other languages: SQL, templates, scripts | — | — | own files, embedded at build time |
| 8 | Secrets | — | CWE-798 | vault |
| 9 | Logs and diagnostics | no, English | OpenTelemetry semantic conventions | event id (category 2) plus structured fields |

**The line.** Categories 1–4, 7 and 8 are stable, never translated, and are
identifiers or data. Categories 5 and 6 are human-facing and locale-dependent.
Logs (9) sit on the line: they carry category-2 identifiers, so a message can be
rendered in any language later and grep or an AI can search for `E-SEC-005`
instead of a sentence someone rewrote. Rule of thumb (W3C i18n best practices):
**never use display text as an identifier.**

### 2. Canonical storage, local display

| Kind | Store as | Display with |
|------|----------|--------------|
| Instant | UTC, RFC 3339 / ISO 8601 (`2026-11-21T09:44:59Z`) | IANA time zone of the user (`Europe/Amsterdam`) and CLDR date pattern |
| Calendar date | ISO 8601 date (`2026-11-21`) | CLDR date pattern (`21-11-2026` for nl-NL, `11/21/2026` for en-US) |
| Money | integer minor units plus ISO 4217 code (`1250 EUR`) | CLDR currency pattern (`€ 12,50` nl-NL, `12,50 €` de-DE, `£12.50` en-GB) |
| Quantity | SI unit (BIPM SI brochure), codes per UCUM | CLDR unit preferences per region (metric, US, UK; °C or °F) |
| Language | BCP 47 tag (`en-GB`, `nl-NL`) | localised name from CLDR |
| Country or region | ISO 3166-1 alpha-2 (`GB`, `NL`); subdivision ISO 3166-2 (`GB-ENG`) | localised name from CLDR |
| Phone number | E.164 (`+31…`) | national format (libphonenumber metadata) |

### 3. Normalised reference data ("the lists")

keur ships reference packs as data under `reference/`, one directory per list, each
with the data file, a `SOURCE.md` (source URL, version or date, licence, refresh
cadence) and a refresh script. Rules reference a pack by id; no list is hardcoded
in the engine (`KEUR-ARCH-005`).

| Pack | Content | Authoritative source | Example of what it settles |
|------|---------|----------------------|----------------------------|
| `iso639` | language codes (639-1 two letters, 639-3 three letters) | ISO 639 maintenance agencies (Library of Congress for 639-2, SIL for 639-3) | `en` is English; `uk` is **Ukrainian**, not the United Kingdom |
| `bcp47` | language subtag registry: languages, scripts, regions, variants, deprecated tags | IANA Language Subtag Registry (RFC 5646); Unicode `-u-` keys from CLDR | `en-UK` is invalid, `en-GB` is correct; `iw` is deprecated for `he` |
| `iso3166` | countries (alpha-2, alpha-3, numeric, official names) and subdivisions | ISO 3166 Maintenance Agency; distributed via CLDR and Debian `iso-codes` | the country is `GB`, official name "United Kingdom of Great Britain and Northern Ireland"; `UK` is only *exceptionally reserved*; England is the subdivision `GB-ENG`, Scotland `GB-SCT`, Wales `GB-WLS`, Northern Ireland `GB-NIR` |
| `iso4217` | currencies, numeric codes, minor units, historic codes | ISO 4217 maintenance agency (SIX) published lists | `EUR` has 2 minor units, `JPY` 0, `BHD` 3; `NLG` is historic |
| `cldr` | per-locale patterns: dates, numbers, currency symbol and position, units, plural rules, first day of week, measurement system | Unicode CLDR (Unicode licence) | `€` before or after the amount; plural categories `one`/`other` for nl, more for pl or ar; week starts Monday in NL, Sunday in US |
| `tzdb` | time zone ids, links (aliases), and Windows mappings | IANA Time Zone Database (public domain); CLDR `windowsZones` | `Europe/Amsterdam` is valid; `CET`, `BST`, `IST` are ambiguous abbreviations, not ids |
| `units` | SI base and derived units, UCUM codes, CLDR unit ids | BIPM SI brochure, UCUM, CLDR | `km/h` vs `kilometer-per-hour`; `°F` is not SI |
| `m49` | world regions | UN M.49 | `150` Europe, `419` Latin America (used in `es-419`) |
| `holidays` | public holidays per country **and subdivision**, as rules (fixed date, Easter offset, nth weekday, substitution rules) with validity years | no international standard; per-country law. Candidate sources: Nager.Date, python-holidays (both open-source), checked against government publications | King's Day in NL; bank holidays differ between England and Wales, Scotland and Northern Ireland |

Each pack has a **freshness rule**: tzdb publishes several releases a year, ISO 4217
amends a few times a year, holidays change by law. A pack older than its cadence is
a `warn` in `keur doctor`, so the checks do not silently go stale.

Licence check before vendoring: IANA tzdb is public domain, CLDR uses the Unicode
licence, the ISO code lists are freely redistributed through CLDR and `iso-codes`;
holiday datasets must be checked one by one.

### 4. What keur checks (proposed rules)

Engines: `pattern` and `multiline` for code shapes; the literal pipeline (ADR-058)
to extract literals; `cross-reference` (ADR-022) for "value ⊆ reference pack".

| Proposed id | Category | Check | Engine | Maturity, action |
|-------------|----------|-------|--------|------------------|
| `KEUR-TXT-001` | 5 | human-text literal (longer than about five words) in `printf`, `cout`, `cerr`, `echo`, `console.log` outside a catalog or templates module | pattern | 3 warn, 5 block |
| `KEUR-TXT-002` | 5 | catalog completeness: every key exists in every shipped language | cross-reference | 4 warn |
| `KEUR-TXT-003` | 5 | placeholders identical across translations of one key (`{version}` in `en` and `nl`) | cross-reference | 4 block |
| `KEUR-TXT-004` | 5 | plural forms present per CLDR plural rules of each language | cross-reference against `cldr` | 5 warn |
| `KEUR-TXT-005` | 5 | catalog key unused, or two keys with the same value (promote to a shared term) | cross-reference | 4 warn |
| `KEUR-LOC-001` | 6 | language tag not valid BCP 47 (`en-UK`, `EN_us`, `nl_NL` in data files) | cross-reference against `bcp47` | 3 block |
| `KEUR-LOC-002` | 6 | country code not ISO 3166-1 (`UK`, `EN`) or subdivision not ISO 3166-2 | cross-reference against `iso3166` | 3 block |
| `KEUR-LOC-003` | 6 | currency code not ISO 4217, or historic | cross-reference against `iso4217` | 3 block |
| `KEUR-LOC-004` | 6 | money as floating point (`double price`, `float amount`) in domain code | pattern | 3 block |
| `KEUR-LOC-005` | 6 | hardcoded currency symbol or symbol placement in code (`"€" +`, `+ " EUR"`) | pattern | 4 warn |
| `KEUR-LOC-006` | 6 | time zone abbreviation used as an id (`"CET"`, `"EST"`) or id not in tzdb | pattern, cross-reference against `tzdb` | 3 block |
| `KEUR-LOC-007` | 6 | local or naive time in domain code (`localtime`, `mktime`, `datetime.now()` without zone, `new Date().toString()`) | pattern | 4 warn |
| `KEUR-LOC-008` | 6 | hardcoded display date pattern in code (`"%d-%m-%Y"`, `"MM/dd/yyyy"`) outside the formatter | pattern | 4 warn |
| `KEUR-LOC-009` | 6 | stored or exchanged date not ISO 8601 in data and fixtures | pattern | 4 warn |
| `KEUR-LOC-010` | 6 | hardcoded unit suffix or conversion factor (`" km"`, `* 2.54`, `* 1.8 + 32`) outside the unit formatter | pattern | 4 warn |
| `KEUR-LOC-011` | 6 | hardcoded holiday date list in code instead of the `holidays` pack | pattern | 4 warn |
| `KEUR-VOC-001` | 2 | state or event compared as a string (`state == "INIT"`) instead of an enum generated from the schema | pattern (extends `QUAL-071` beyond JS/TS) | 3 warn, 5 block |
| `KEUR-VOC-002` | 2, 9 | log call without an event id (free sentence only) | pattern | 5 warn |
| `KEUR-VOC-003` | 2 | error returned or thrown without a stable code | pattern | 4 warn |
| `KEUR-REF-001` | all | reference pack older than its refresh cadence | doctor check | 3 warn |

Inline categories 3, 4 and 7 are covered by the rules proposed in
`docs/research/inline-code.md` (extended `QUAL-030` and `QUAL-065`,
`KEUR-INL-001` to `KEUR-INL-005`). Category 8 is covered by the existing secret rules.
How human text (category 5) and codes (category 2) are emitted — diagnostics with
codes, a catalog and a reporter adapter — is decided in ADR-074.

**Implemented so far** (branch `feat/inline-sql-rules`, 2026-11-21):

| Category | Rule | Fixtures | Calibration |
|----------|------|----------|-------------|
| 7 embedded SQL | `QUAL-030` (rewritten), `QUAL-030b` (multiline) | `tests/fixtures/inline-sql` | 13/13 positive, 0 negative |
| 4 endpoints in code | `KEUR-INL-002` | `tests/fixtures/inline-endpoint` | 11/11 positive lines, 0 negative |

### 5. Maturity mapping (CMMI-aligned, ADR-026 and ADR-061)

| Level | What is expected of a repo |
|-------|---------------------------|
| 1 | secrets only (existing rules) |
| 2 | invalid codes (`LOC-001` to `LOC-003`, `LOC-006`) warn |
| 3 | categories separated: invalid codes, float money and time-zone abbreviations block; human text and string-typed states warn |
| 4 | catalogs complete and consistent, canonical storage enforced, formatters used, reference packs fresh |
| 5 | no human-text literal and no string-typed state in code at all (block), plural rules checked, logs carry event ids; a repo at level 5 is fully i18n-ready, so adding a language or locale is a data change only |

Level 5 for this family therefore means: **t9n and l10n are data, codes are valid
against the official lists, values are canonical, and code contains only code.**

## Consequences

- keur gains a `reference/` tree and a refresh routine; the data is larger than the
  rules but changes slowly and is reviewable like rules.
- Checks that need a reference move from guesses to facts: `en-UK` is wrong because
  the IANA registry says so, not because a regex author thought so.
- Rules depending on `cross-reference` need ADR-022 to be implemented first; pattern
  rules (`LOC-004`, `LOC-006`, `LOC-007`, `TXT-001`, `VOC-001`) can ship before it.
- The catalog convention itself (file format, generated typed keys, `${term}`
  references, build-time embedding for C++) is a decision for the C++ family
  scaffold (`cpp-template`); keur only checks that it is used.
- False positives are expected in the gray zones (developer messages, protocol
  constants in adapters); path scope (ADR-043) and per-line `keur:ignore` with a
  reason are the release valves.

## Alternatives considered

- **Only regex rules, no reference data** — cannot tell a valid code from an invalid
  one; would keep accepting `en-UK`.
- **Depend on ICU at check time** — complete, but a heavy dependency in a
  pre-commit hook (ADR-002); the same CLDR data as plain files is enough for checks.
- **Leave l10n to framework linters** (i18next, Android Lint) — covers JS and
  Android only, not C++, shell or data files, and not the reference lists.

## Open questions

1. Holidays: which dataset is authoritative enough to ship, and per which
   subdivisions (NL national; UK per nation)?
2. Should reference packs live in keur, or in a separate data repository that keur
   and loods both consume (loods needs the same lists at runtime for formatting)?
3. Is the five-word threshold for `TXT-001` right, or should any literal containing
   a space count at level 5?
