# Traceability convention — explicit, refactor-proof

keur links decisions, code and tests so we can *measure* whether every decision
is actually built and tested — bidirectional requirements traceability, the
ISO/IEC/IEEE 29148 idea, implemented the OpenFastTrace way but **lightweight**:
grep-able tags in the artefacts themselves, no external tool (that would break
the single-binary rule, ADR-002).

## The one rule: explicit, never hard

- **Explicit annotation only.** A link is a tag a human wrote on purpose
  (`@implements ADR-009`). Nothing is inferred from position.
- **Never a hard annotation.** A link points at a **stable concept ID**
  (`ADR-009`, `KEUR-SEC-006`), never at `file:line` or a location. Move the
  function, split the file, reorder lines — the link survives. Only deleting the
  *concept* (or the file entirely) breaks it, and then it *should* break.

Why: `file:line` rots on the first refactor and produces false "broken links".
A concept ID is what the relationship is actually *about*.

## Identifiers — declared with `@id`, not derived from filenames

An ID is an identity, **not** a filename or a type. Any artefact — an ADR, an
RFC, a policy, a principle, a threat model, or a random paragraph of markdown —
*declares* a concept by writing `@id <token>` anywhere in its text. The token is
free-form: `ADR-009`, `RFC-003`, `principle-least-privilege`, `policy-secrets`,
`threat-ssrf`. One flat namespace of concepts; the tooling knows only "declared"
vs "referenced", never the type.

### Sub-concepts in large documents

A big, multi-part document (ADR-004's Stage 0–6, ADR-009's facts/requires/
composite) declares a **sub-`@id` per independently-implementable part**, using a
dot: `@id ADR-004.stage4`, `@id ADR-009.composite`. Code then references the part
it builds (`@implements ADR-009.facts`), so coverage shows *which stage* is done
and which is still orphan.

Never link to a heading number or anchor (`ADR-001#4`, `#the-decision`): that is a
*position*, and it rots the moment a section is reordered or renamed — the same
"hard annotation" we forbid. A sub-`@id` is a declared identity; it survives the
move. Default to the whole-ADR `@id`; split into sub-`@id`s only when the ADR
genuinely holds separable pieces.

```markdown
<!-- somewhere in docs/adr/ADR-009-composite-rules.md -->
@id ADR-009

<!-- or mid-paragraph in any principles doc -->
We always apply least privilege. @id principle-least-privilege
```

Two exceptions that are declarations by construction (no separate `@id` needed):

| Artefact | Declares its ID via | Why |
|----------|--------------------|-----|
| Rule (`.rule`) | the `id:` field | already unique + stable, the rule's identity |
| ADR file | may use `@id`, else the `ADR-NNN` in its filename | filesystem already enforces uniqueness |

**Code and tests never declare an ID.** They *refer* (a verb) or are *tested*
(`[feature:]`). The relationship is always referrer → concept, so only the
concept needs an ID. A token is an ID **only** if it was declared with `@id`
(or a rule `id:` / ADR filename) — a bare `ADR-003` inside a path fragment or in
prose is just text, not a reference.

## The verbs (all grep-able, all comment-styles)

- **`@id <token>`** — *declares* a concept here. Must be unique across the repo
  (a duplicate is a bug — see `KEUR-DOC-002`).
- **`@implements <token>`** — this code/rule realises that concept. First token
  after the verb; must resolve to a declared `@id`. Closes the concept→code loop.
  ```cpp
  /**
   * @file rule/composite.cpp
   * @implements ADR-009
   */
  ```
  ```yaml
  id: KEUR-SEC-006
  # @implements ADR-003 ADR-005
  ```
- **`@proves <id>`** — a test *proves* that concept works. Closes the concept→test
  loop, and it is **how-independent**: the tag counts wherever it lives — a C++
  `SCENARIO`, a shell test, an integration test, or a `.rule` self-check. The
  meter counts the claim, never the mechanism. One or more IDs, space-separated.
  ```cpp
  SCENARIO("[feature:rule-engine] presence fires once per file") {  // @proves engine-presence
  ```
  ```bash
  # test/hooks.sh — @proves engine-file-presence
  ```
- **`[feature:<name>]`** — a coarse test grouping tag (already used in
  `keur_test.cpp`). Useful for running/filtering suites; `@proves` is what the
  coverage meter reads, because it names the *concept* proven, not just the suite.
- **`@see <token|path|url|text>`** — a weak "see also" (Doxygen sense). Points at
  **anything**: a declared concept, a file, a URL, a free name
  (`@see https://nextjs.org/docs`, `@see nextjs-routing-guide`). Validated for a
  dead link **only** when its first token is a declared `@id`; a path/URL/free
  name is always valid and not checked.
- **`@external <repo>:<token>`** — a deliberate reference to an ID-bearing concept
  in *another* repo (`@external cpm:ADR-170`). The `<repo>:` prefix makes it
  unambiguous; it is recorded but never validated against keur's own `@id` set.

No `@proves` / `@explains`: `[feature:]` already means "tested", a doc already
means "explains". The verbs, each with one meaning: `@implements` (builds a keur
decision), `@see` (related — anything, unvalidated), `@external` (another repo's
ID-bearing concept).

**Only tagged references count.** A bare `ADR-005` in prose is *not* a link — the
harvester scans the verbs (`@implements` / `@see` / `@external`), never loose
`ADR-NNN` text. This is what keeps cross-repo prose (`cpm ADR-170` in a comment)
out of the numbers without any name heuristic: if it is not on a verb, it is not
a link.

One grep pulls every claim regardless of `//`, `/** */` or `#`:

```bash
grep -rhoE '@implements[[:space:]]+[A-Za-z0-9 :_-]+' src/ rules/ | grep -oE 'ADR-[0-9]+' | sort -u
```

## The `Coverage:` header — honest exceptions

Some ADRs are pure build/process decisions with no code site (ADR-002 single
binary, ADR-008 incremental compile). They declare this in the header so the
coverage report stays honest instead of showing a false gap:

```markdown
**Status**: accepted · **Coverage**: none    # deliberate: no code site
```

Values: `code` (must have ≥1 `@implements` pointing at it) · `none` (deliberate,
no code) · absent = treated as `code` (so a new ADR is "uncovered" until built —
the gap is the point).

## Measuring it (bidirectional coverage)

`eval/doc-coverage.sh` reduces everything to set arithmetic:

- **doc→code**: ADRs with `Coverage: code` that have ≥1 `@implements`.
  A `code` ADR with none = **orphan decision** (built nothing yet).
- **code→doc**: every `@implements`/`@see` ID resolves to a real ADR/rule.
  One that doesn't = **dead link**.
- **concept→test**: every engine/command has a `[feature:]` test.
  One that doesn't = **untested trick**.

`--strict` turns any sub-100% into exit 1 (gate mode). The same check can later
live as a `KEUR-DOC-*` self-check rule so keur guards its own traceability with
its own engine.

## How links are seeded (never auto-claimed)

Existing ADR references already live in the code as `@see`. Upgrading a `@see` to
`@implements` is a *semantic* judgement (does it build the decision, or just
mention it?), so it is **never** done automatically. `eval/xref-harvest.sh` may
*propose* candidates (e.g. files under `adapters/` that reference ADR-003), but a
human accepts the diff. Evidence proposes; a human decides — the same discipline
as the rule engine.
