# ADR-058 — Literal-frequency scanner: extract literals, strip code+comments, subtract an allowlist, surface the residue

@id ADR-058

## Status

Proposed · 2026-10-10 · Coverage: none
· Relates to: ADR-042 (PII-vault scanner — the denylist counterpart; this is its
inverse), ADR-046 (.keurignore allow — the per-repo allowlist primitive this
reuses), ADR-019 (external scanners + dispatch — a new evidence source),
ADR-009 (rules are data), ADR-035 (self-explaining findings + suppression),
ADR-052 (scan scope: file-level vs per-line), `src/rules/tokenizer.{h,cpp}`
(the comment/string state machine this builds on)

## Context

The PII-vault scanner (ADR-042) is a **denylist**: it flags literals that match a
pattern you already knew to list (`work`, `personal`, `gdpr-nl/uk`,
`generic`). It cannot catch an identifier you did not know was hardcoded — a
system user-id like `2650567`, an SSO principal `AROATYEDKBRKNFSHCHAVC`, an
internal account number, a one-off ARN — because nobody put it in a `.pii` list.
These are exactly the leaks that slip through: values that are *linked to a
person or a system* but are not in any known-bad corpus.

The complementary question is not "does this literal match a known-bad pattern?"
but **"what literals does this codebase actually contain, and which of them are
not on a known-good list?"** That inverts the problem: instead of enumerating
badness, enumerate *everything*, subtract the known-good, and let a human look at
the residue. A hardcoded identity value shows up as an unfamiliar literal — often
appearing exactly once (a copy-pasted id) or clustered in a fixture.

Frequency matters. A literal that appears 200 times is almost certainly
structural (a key name, a common path, a status string). A literal that appears
**once or twice**, is long, and is non-dictionary (a number ≥6 digits, a
base32/hex blob, an ARN) is the interesting residue. So the output is not just
"which literals" but **"which literals and how often"** — the count is the triage
signal.

We already own the hard part. `src/rules/tokenizer.cpp` is a single-pass,
language-aware state machine (15+ languages) that can strip comments and strings
while preserving line structure. It exposes `strip_comments` (strings kept) and
`strip_comments_and_strings` (both gone). What this scanner needs is the **third
projection**: strip comments and strip *code*, keeping only the **literals** —
the inverse of what we have. Same state machine, new emitter.

## Decision

Add a **literal-frequency analysis** to keur that:

1. **Strips comments** (reuse the tokenizer; comments are commentary, not shipped
   values — and a hardcoded id in a comment is still worth seeing, so comment
   stripping is configurable, default on for the code pass).
2. **Extracts literals**, not code: string-literal contents (between the
   language's string delimiters) plus bare numeric/opaque tokens that look like
   identifiers — numbers ≥ N digits, hex/base32 blobs, UUIDs, ARNs. Everything
   that is syntax (keywords, operators, identifiers that are variable names) is
   dropped.
3. **Normalises and counts**: trim, collapse, `sort | uniq -c` semantics — one
   row per unique literal with its occurrence count across the scanned scope.
4. **Subtracts an allowlist** of known-good literals (see "Allowlist source"),
   like a spellchecker dictionary (ADR-046 model). What remains is the residue.
5. **Surfaces the residue** sorted by rarity (lowest count first) and length
   (longest first) — the triage order, because rare + long = most suspicious.

Ship it **first as an audit command, not a gate** (ADR-052 scope note): a human
reviews the residue before any blocking behaviour exists. A gate can follow once
the allowlist is tuned and the false-positive rate is known.

### Two phases, explicit

- **Phase 1 — `keur literals` (audit, non-blocking).** Prints the literal→count
  table (optionally residue-only after allowlist subtraction). Read-only, no
  pipeline wiring, no commit impact. This is where the user *sees what is in the
  codebase* and decides what is allowlist vs leak. Cheap, reversible, honest.
- **Phase 2 — dispatch scanner (opt-in gate), only after Phase 1 proves out.**
  A `[scanners.literal-residue]` source (off by default, like ADR-042's
  pii-vault) that, on the staged diff, blocks when a *new* residue literal above
  a rarity threshold is introduced. Honours `keur:ignore` and `.keurignore allow`
  (ADR-046) for the same value-suppression UX.

### The new tokenizer projection

Add `extract_literals(content, syntax)` next to the existing two strippers:
same O(n) state machine, but instead of blanking strings it **emits** them and
blanks everything else. Line structure preserved so each literal keeps its
`file:line`. Comment handling reuses `strip_comments` first. No new language
table — the existing `LangSyntax` already knows the delimiters.

### Allowlist source (open question → decision)

Three candidates were considered:

- **`$PII_VAULT`** — wrong direction; that is the *denylist* (known-bad), not
  known-good.
- **`.keurignore allow <term>`** (ADR-046) — already the per-repo "this value is
  fine here" dictionary. **Reuse this** for hand-curated allows (key names,
  common paths, status strings that are obviously structural).
- **A generated baseline** — on first run, `keur literals --baseline` writes the
  current literal set to `.config/literal-baseline` so subsequent runs show only
  *new* literals (the "what changed" view, like a snapshot). Useful for an
  existing repo with a large legacy literal set you do not want to triage all at
  once.

**Decision:** reuse `.keurignore allow` for curated allows (one mechanism, ADR-046)
**and** support an optional generated baseline for drift-only review. No new
hand-maintained allow file (ADR-046 already warned against suppression sprawl).

## Why not just extend ADR-042

ADR-042 is pattern→match (denylist). This is enumerate→subtract (allowlist
residue). They answer opposite questions and both are needed: the vault catches
known-bad even when it is rare; the residue catches unknown-bad precisely because
it is rare. Folding one into the other would blur two detection models that fail
in different ways.

## Consequences

- **Positive**: finds hardcoded identity values that no denylist contains, using
  frequency as the triage signal — the thing the user actually asked for.
- **Positive**: reuses the proven tokenizer and the ADR-046 allow primitive; the
  only genuinely new code is one emitter projection + a count/sort/subtract step.
- **Positive**: audit-first (Phase 1) is read-only and reversible — no commit is
  ever blocked until the model is validated against a real repo.
- **Negative / cost**: literal extraction is noisy on string-heavy code (i18n
  tables, SQL, test fixtures); without a good allowlist/baseline the residue is
  large. Mitigated by: frequency sort (structural = high count = bottom),
  length/charset heuristics (short dictionary words filtered), and the baseline
  for legacy repos.
- **Negative / cost**: numeric/opaque heuristics (what counts as "identifier-like")
  need tuning; start conservative (≥6-digit numbers, UUIDs, ARNs, ≥16-char
  base32/hex) and widen from Phase-1 evidence.
- **Trust**: Phase 1 never blocks and never echoes a value into a tracked file;
  the residue table is for the human running the command. A gate (Phase 2) uses
  the same no-echo discipline as ADR-042 (report `file:line` + count, not the
  value) so the gate output itself cannot leak.

## Amendment 2026-10-10 — classify first, then apply a per-class rule

Phase-1 iteration on the security-tooling repo (9651 unique literals) showed that
a single frequency threshold is too blunt: the residue mixes two *opposite*
problems that need opposite rules. The fix is a **classification layer** between
"count" and "report". Each unique literal is assigned exactly one **class**
(first match wins, order = priority), and each class maps to one of three
**buckets** with its own rule. Classification is what turns 9651 raw literals
into ~33 identity flags + a short magic-value list — it IS the automation.

### The three buckets

| Bucket | Problem signal | Rule | Advice |
|--------|----------------|------|--------|
| **NOISE** | — | drop entirely | (ignored) |
| **MAGIC** | **high** frequency (≥3× = rule of three) + same concept, esp. case variants | centralise | "appears N× → extract to a named constant/enum" |
| **IDENTITY** | **low** frequency (1–2×) + identifier-shaped | should not be in code | "belongs in config/vault, not source" |

Note the inverted frequency logic: for MAGIC high count is the smell (drift risk,
e.g. `stopped`/`STOPPED`); for IDENTITY low count is the smell (a one-off pasted
id). Same data, opposite ends of the distribution — which is exactly why one
threshold cannot serve both.

### The class table — an engine (C++) + rules (data), not either alone

The split follows keur's own architecture (ADR-014 engine registry, ADR-009
rules-are-data), and it is deliberately a **collaboration**, not hardcoded C++
nor a throwaway script:

- **C++ side — one new engine `literal-classify`.** It knows the *mechanism*:
  run `extract_literals`, normalise, count per unique literal, match each against
  the ordered class patterns it is given, route to a bucket, apply the bucket's
  threshold, emit findings. It does NOT know any class names, regexes, or
  thresholds — those are inputs. One entry in `rule/engines.cpp`, like every
  other engine. (Precedent: the existing `extract-duplicates` engine.)
- **Data side — `.rule` files carry the class table.** Each class is a row in a
  rule's data: `regex`, `bucket` (noise|magic|identity), optional `threshold`
  (rule-of-three default 3), `advice`. New classes, per-repo tuning, and
  threshold changes are data edits — no engine rebuild. The seed table below is
  the validated starting set; it ships as data and evolves there. The rule lives
  in `rules/generics/literal-classes.rule` as a **generic rule** (ADR-059): it
  feeds the engine and never fires on its own, and is overridable at repo or
  global level without a compile.

Measured distribution on security-tooling (uniq counts) in parentheses:

| Class | Bucket | Match (first wins) | Example |
|-------|--------|--------------------|---------|
| `BLANK` | NOISE | whitespace only | `" "` |
| `LANG_KEYWORD` | NOISE | `null`/`true`/`false`/`nil`/`none`/`undefined` | `null` (8) |
| `SMALL_NUM` | NOISE | integer < 4 digits | `1`, `30` (27) |
| `TEMPLATE/VAR` | NOISE | starts `$`, or has `%s`/`%d`/`{{`/`}}` | `$1`, `%s` (2607) |
| `SYMBOL/FRAGMENT` | NOISE | punctuation / ANSI / markup / shell shards | `)`, `033[0m`, `</ul>` |
| `SENTENCE/MSG` | NOISE¹ | contains spaces (prose) | log/error text (4130) |
| `AWS_ARN/KEY` | IDENTITY | `arn:aws`/`AROA`/`AKIA`/`AIDA` prefix | (1) |
| `EMAIL` | IDENTITY | `…@domain.tld` | `david.odibo@…` (22) |
| `LONG_NUM(id?)` | IDENTITY | integer ≥ 6 digits | `257973423188` (10) |
| `HOSTNAME/DOMAIN` | IDENTITY | ends `.local`/`.internal`/`.com`/…, no spaces | `vault.internal.example` (61) |
| `URL` | IDENTITY | `http(s)://…` | endpoints (99) |
| `PATH` | IDENTITY | starts `/`, `~/`, `./` | (80) |
| `WORD(const?)` | MAGIC | `^[A-Za-z][A-Za-z0-9_]*$`, ≤24 chars | `stopped`, `clickup-api-key` (1148) |
| `OTHER` | review | anything unmatched | (shrinks as table grows) |

¹ `SENTENCE/MSG` is NOISE for this tool, not "fine" — it is the i18n question,
deliberately out of scope here (see the explicit non-goal below).

### Case-audit is a MAGIC sub-view

Group `WORD` literals by `tolower()`; where >1 original-case variant exists
(`stopped`/`STOPPED`, `high`/`High`/`HIGH`), that is the strongest extract-to-enum
candidate — one concept spelled inconsistently, a drift bug waiting to happen.

### Explicit non-goals (the boundary the user asked for)

- **Not i18n.** `SENTENCE/MSG` is dropped, not routed to a message catalogue.
  This tool is about single-source-of-truth for *values*, not translation.
- **Not auto-extraction.** The tool classifies, counts, and advises; a human
  decides whether two equal literals are the *same concept* (coincidentally-equal
  values with different meaning must NOT be merged — that couples things that
  should vary independently).
- **Thresholds are config.** Rule-of-three default is 3, overridable; the class
  table is data so new classes/repos tune without an engine rebuild.



1. `extract_literals(content, syntax)` in `src/rules/tokenizer.{h,cpp}` — the
   third projection (emit strings + identifier-like tokens, blank the rest).
   Unit-test against each language family (comment/string/triple-string edges).
2. `keur literals [path]` audit command: extract → normalise → count →
   **classify (class table) → route to NOISE/MAGIC/IDENTITY** → subtract
   `.keurignore allow` → report per bucket. Flags: `--bucket magic|identity`,
   `--case-audit`, `--min-count` (rule-of-three, default 3), `--baseline`
   (write), `--since-baseline` (diff).
3. Split the classifier the keur way: a C++ engine `literal-classify` in
   `rule/engines.cpp` (mechanism only — extract, count, match-against-given-table,
   route, threshold) + `.rule` data files carrying the class table (regex →
   bucket → threshold → advice), seeded from the Amendment 2026-10-10 table
   (validated on security-tooling). Classes/thresholds tune as data, no rebuild.
   Shrink `OTHER` from evidence across repos.
4. (Phase 2, separate ADR amendment) `[scanners.literal-residue]` dispatch
   source, opt-in, staged-diff scope, `.keurignore allow` + `keur:ignore`
   suppression, rarity threshold → block.
5. Docs: a CONTRIBUTING note — "find unknown hardcoded identifiers:
   `keur literals --residue-only`; allow structural ones with `allow <term>`."
