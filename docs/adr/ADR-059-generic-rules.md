# ADR-059 — Generic rules: a rule that feeds an engine's logic instead of firing on its own

@id ADR-059

## Status

Proposed · 2026-10-10 · Coverage: none
· Relates to: ADR-009 (rules are data; composites keep logic in data not C++),
ADR-014 (engine registry — engines are the mechanism), ADR-043 (path-scoped
config — the override surface), ADR-051 (rules distribution + `$KEUR_RULES_DIRS`
overlay — the extend surface), ADR-052 (scan scope), ADR-058 (the first
consumer: the literal-classify engine's class table)

## Context

Some engine behaviour is driven by a **table of knowledge** that must not be
hardcoded in C++ but also is not a per-finding scan rule. ADR-058's classifier is
the motivating case: the *mechanism* (extract literals, count, match a class,
route to a bucket, apply a threshold) belongs in a C++ engine, but the *class
table* (which regex means EMAIL vs LONG_NUM vs WORD, which bucket, which
threshold) is knowledge that:

- must be editable **without a compile or release** (tune a threshold, add an
  org-specific client-id shape, add an internal domain),
- must be overridable at **repo level** and **global level** (a work repo knows
  its own id formats; a personal repo does not want them),
- is still fundamentally **a rule** — it has an id, a category, provenance, and
  lives in `rules/` under version control like every other rule.

Today keur has no first-class notion of a rule that *provides data to an engine*
rather than *firing on code*. The rule loader (`find_rule_files`) is recursive
and hands **every** `.rule` to engine dispatch, so a naively-placed class-table
rule would try to "fire" as if it were a scanner. Composites (ADR-009) come
closest — logic as data, engine stays simple — but a composite still produces its
own finding by combining leaves; it is not a passive table an engine reads.

We want: the logic stays in a rule (data, versioned, overridable), the C++ stays
generic (mechanism only), and the rule is **not directly usable on its own** —
it does nothing until an engine consumes it.

## Decision

Introduce **generic rules**: rules that configure an engine mechanism instead of
firing standalone. Three parts, all reusing existing machinery:

### 1. A home: `rules/generics/`

A dedicated directory signals intent to humans ("these are mechanisms, not
checks") — the same categorical role `rules/keur-internal/` already plays, but
for engine-feeding tables rather than keur's self-checks. Organisational, and it
keeps generic rules findable and reviewable.

### 2. A marker: the rule is inert to standalone dispatch

A generic rule is identified by its `engine:` naming a **consumer engine** plus a
`generic: true` marker. Dispatch skips `generic: true` rules in the normal
per-line/per-file scan loop — they emit no finding by themselves. They are loaded
and parsed like any rule (so they are validated, versioned, listable), but they
sit in a side table keyed by id, waiting to be read by the engine that declares
them as input. "Not directly usable" is enforced, not just convention.

```
id: literal-classes
category: quality
engine: literal-classify     # the consumer engine
generic: true                # inert on its own; feeds the engine
classes:                     # the data the engine reads
  - name: EMAIL      bucket: identity  regex: '@[\w.-]+\.[a-z]{2,}'  advice: "config/vault, not source"
  - name: LONG_NUM   bucket: identity  regex: '^[0-9]{6,}$'          advice: "looks like an id — externalise"
  - name: WORD       bucket: magic     regex: '^[A-Za-z][\w-]{0,23}$' threshold: 3  advice: "appears N×, extract to a constant/enum"
  # ...
```

### 3. Override + extend without compile: the existing cascade

- **Global override**: a generic rule in the global rules dir (resolved via the
  install sync / `$KEUR_RULES_DIRS`, ADR-051) replaces the shipped base by id.
- **Repo override**: a repo-local `rules/generics/literal-classes.rule` with the
  same id overlays the base (ADR-051: an intentional overlay is legitimate, never
  a block). Path-scoped config (ADR-043) decides *where* that applies.
- **Edit, no release**: because it is data read at scan time, changing a regex,
  a bucket, or a threshold takes effect on the next `keur` run — no rebuild, no
  version bump. This is the whole point.

Resolution order mirrors config (ADR-043/051): built-in base → global → path-scoped
→ repo. Later wins, by rule id.

### What stays in C++ vs data

- **C++ (generic, compiled once)**: the engine mechanism — how to extract, count,
  match a regex, route to a bucket, apply a threshold, emit. Knows no class names.
- **Data (`rules/generics/*.rule`, editable anytime)**: which classes exist,
  their regexes, buckets, thresholds, advice. The knowledge.

## Why not the alternatives

- **Hardcode the table in C++**: the thing the user explicitly rejected — a
  threshold or a new id-shape would need a compile + release. Fails the core
  requirement.
- **A throwaway script / sidecar file**: not versioned as a rule, not listable,
  no provenance, no override cascade. Rejected (and the user does not want shell).
- **A plain rule in a normal category**: the recursive loader would fire it as a
  scanner and it would produce bogus findings. The `generic: true` marker is what
  makes it inert.
- **Reuse `composite`**: composites still emit by combining leaf findings; a
  generic rule emits nothing — it is read, not fired. Different shape.

## Consequences

- **Positive**: engine logic becomes data-driven and tunable at global/repo level
  with zero compile — exactly the asked-for property. The class table (and any
  future engine table) lives as a reviewable, versioned rule.
- **Positive**: reuses the loader, the id-override overlay (ADR-051), and the
  path-scoped cascade (ADR-043); the only new surface is the `generic: true`
  marker + a side-table lookup the consuming engine does.
- **Positive**: generalises beyond ADR-058 — any future engine that needs a
  tunable knowledge table (not just literal classification) uses the same
  pattern.
- **Negative / cost**: dispatch must learn to skip `generic: true` rules and
  expose them to engines by id; the parser must accept the generic payload shape
  (a list of rows). Both are small, both need tests (a generic rule produces no
  standalone finding; an engine reads its table; a repo overlay wins by id).
- **Trust / footgun**: a generic rule is only as safe as the engine reading it —
  a bad regex in a class table degrades classification, it does not execute code.
  Keep generics declarative (regex + enum fields), never anything executable.

## Follow-ups

1. Parser: accept `generic: true` and an engine-specific payload (for
   literal-classify: a `classes:` list of `name/bucket/regex/threshold/advice`).
   Validate; unit-test the parse (rows, defaults, bad regex rejected).
2. Dispatch: skip `generic: true` in the standalone scan loop; register generics
   in a by-id side table engines can read. Test: a generic rule yields no
   finding on its own.
3. `rules/generics/` directory + the seed `literal-classes.rule` (lowercase,
   kebab-case filename) carrying the ADR-058 class table.
4. Override/extend proof: a repo-local `rules/generics/literal-classes.rule`
   overlays the base by id (ADR-051); a global one applies everywhere; e2e test
   that an edit changes behaviour with no rebuild.
5. Document in rules-authoring: "a generic rule feeds an engine; mark it
   `generic: true`; it never fires on its own; override it by id at repo/global
   level."
