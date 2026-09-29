# ADR-038 — Per-dimension difficulty: choose a level per concern, not one knob for all

@id ADR-038

## Status

Proposed · 2026-09-29 · Coverage: none
· Relates to: ADR-011 (project profile — the axes; categories = the dimensions here),
ADR-026 (rule maturity levels — the `<=` band this generalises),
ADR-037 (difficulty as an agent briefing — the briefing renders whatever this selects),
`docs/design/doom-level-select.md` (the single-knob chooser — the "rookie" tier),
`docs/design/keur-manager-tree-tui.md` (per-rule toggling — the "expert" tier)

## Context

keur has two ways to set strictness, at opposite ends of a spectrum:

- **The doom "Choose your Difficulty" chooser** (`doom-level-select.md`): one
  knob, five levels, applied to the *whole* rule corpus at once (ADR-026's
  `maturity <=` band). Fast, friendly, zero-config — but coarse. It is
  full-stack: picking "KEUR Me Plenty" moves security, quality, traceability,
  hardening — everything — to level 3 together.
- **The keur manager TUI** (`keur-manager-tree-tui.md`): every single rule
  toggled by hand. Total control — but it does not scale to ~1000 rules and is
  far more than most projects want.

Nothing sits between them. Real projects need the middle: **"security at level 3,
but traceability off — we cover that with another control; hardening at level 4
because we ship publicly."** The one-knob chooser cannot express that; the
per-rule manager can, but only by hand-picking hundreds of rules. The tension the
collaborator named: a **rookie** wants one knob, an **expert** wants every rule,
and a **medior** wants something granular but not exhausting — a level *per
concern*.

The concerns already exist as keur's rule **categories** (ADR-011 calls them
controls: `secret`, `cmdi`/`sqli` injection, `iac`/hardening, `pii`, `transport`,
plus the process controls `traceability`, `tests`, `regression`, `quality`). Each
is an independent dimension along which a project can legitimately want a
different bar. Today they all move together because the difficulty is a single
scalar. Making it a *vector* — one level per dimension — is the missing middle.

## Decision

Difficulty is expressed at **three tiers of granularity**, each a refinement of
the one before, all resolving to the same underlying per-rule active/action state
the gate reads. A project picks the tier that fits; they compose, they don't
compete.

```text
ROOKIE  (one knob)      MEDIOR (a vector)              EXPERT (per rule)
doom level select   →   level PER dimension        →   keur manager TUI
"KEUR Me Plenty" = 3    security=4 quality=3            toggle KEUR-SQLI-003
                        traceability=off pii=5          individually
```

### 1. The dimensions = rule categories (ADR-011 controls)

A **dimension** is a rule category. The initial set (the ADR-011 controls):

```text
security      (secret, cmdi, sqli, xss, … — the vulnerability families)
hardening     (iac, transport, container, headers)
pii           (data-class handling)
quality       (file/dir size, nesting, dead code, duplication)
traceability  (@id/@implements/@proves coverage — ADR-010)
tests         (tests present / coverage)
```

The set is data, not code (ADR-009): it is derived from the categories present in
the loaded rule corpus, so adding a category to the rules adds a dimension to the
chooser with no engine change.

### 2. MEDIOR tier — a difficulty *vector* in the profile

Instead of one `maturity = 3`, a project may set a level *per dimension*. Each
dimension takes a maturity level `1..5` (the ADR-026 scale) **or** `off`:

```toml
[difficulty]                 # the MEDIOR tier — a level per concern
security     = 4             # stricter: we ship publicly
hardening    = 4
quality      = 3             # the sensible middle
traceability = "off"         # covered by another control; don't gate it here
pii          = 5
tests        = 3
```

Resolution: a rule is active when `dimension_level(rule.category) >= rule.maturity`
(ADR-026's `<=` band, now evaluated per dimension instead of one global level);
`off` disables the whole dimension. `action` still decides the verdict once active
(ADR-026 §3 — orthogonal). This is a strict generalisation: the single-knob
chooser is the special case where every dimension gets the same level.

`off` is first-class and honest: it encodes "we deliberately don't gate this here
because another control covers it" — a real, defensible posture, not a loophole.
keur records it (and the agent briefing, ADR-037, states "traceability: not
enforced — covered elsewhere"), so the choice is visible, not silent.

### 3. The three tiers compose (rookie → medior → expert)

They are layers of override, not separate systems — same cascade spirit as
ADR-011:

1. **Rookie**: the doom chooser sets *every* dimension to the chosen level (writes
   the vector with all-equal values). A beginner never sees the vector.
2. **Medior**: the user overrides individual dimensions in `[difficulty]`
   (the chooser gains a second screen: after picking a base level, optionally
   tune per dimension — arrow to a dimension, set its level or `off`).
3. **Expert**: the manager TUI overrides individual *rules* on top of the vector
   (a specific rule forced on/off regardless of its dimension's level).

Precedence, later wins: base level → per-dimension vector → per-rule manager
override. So the medior vector is "set the band per concern"; the expert manager
is "and hand-tune these specific rules". A project can live entirely at one tier.

### 4. Shareable difficulty sets

Because the vector is plain data, a `[difficulty]` block is a shareable
**difficulty set** — the ADR-026 profile templates (`prototype`, `standard`,
`hardened`, …) become the all-equal special cases, and a team can ship its own
(`org-public-service.toml`: security=4, hardening=4, traceability=off because a
separate portal enforces it). Named sets resolve like profiles (global → repo →
explicit), so "security level 3, traceability off" is defined once and reused.

## Why not the alternatives

- **Keep only the single knob (status quo doom chooser)**: cannot express
  "security 4, traceability off" — the exact real-world need. A scalar collapses
  independent concerns into one, forcing a project to over- or under-gate whole
  categories to satisfy one.
- **Only the per-rule manager for granularity**: does not scale to ~1000 rules
  and overwhelms the medior who wants "stricter on security, looser on quality"
  in four lines, not four hundred toggles. The manager stays the expert escape
  hatch, not the day-to-day control.
- **A second orthogonal scalar (e.g. a global "security bar" separate from a
  "quality bar")**: arbitrary and non-extensible — why two and not one per
  category? The dimensions already exist as categories; use them, and new
  categories extend the vector for free (ADR-009).
- **Free-form per-rule tags/groups**: less ordinal and harder to reason about
  than "level N per known dimension"; the manager already covers arbitrary
  per-rule choice for the rare case that needs it.

## Consequences

**Positive**
- The missing middle exists: a medior sets a level per concern in a few lines —
  granular without the per-rule burden — while rookie (one knob) and expert
  (per rule) are unchanged and still valid.
- `off` per dimension makes "covered by another control" an explicit,
  auditable posture rather than a silent gap or a bypass.
- Strict generalisation of ADR-026: the global band is the all-equal vector, so
  nothing existing breaks; the gate still reads per-rule active/action state.
- Composes cleanly with ADR-037: the agent briefing renders *whatever* the vector
  resolves to, so an agent is told "security is strict here, traceability is off"
  — per-dimension guidance, not a blanket level.
- Difficulty sets are shareable data (ADR-009), so an org standardises
  "security 4 / traceability off" once and reuses it.

**Negative / cost**
- The policy/ruleset layer moves from one global level to a per-category lookup
  when deciding a rule's active state — a map keyed by category, evaluated the
  same `>=` way (ADR-014 dispatch: a lookup table, since the list grows + it is a
  data branch, not behaviour). Modest.
- The chooser gains a second (optional) screen for per-dimension tuning; it must
  stay skippable so the rookie one-knob flow is untouched.
- The dimension set must stay in sync with the rule categories; derive it from
  the loaded corpus (data) rather than hard-coding a list, so a new category
  cannot silently lack a dimension.

## Follow-ups

1. `[difficulty]` vector parsing in the profile loader; resolve per-rule active
   state as `dimension_level(category) >= rule.maturity`, `off` disables.
2. Doom chooser: after the base level, an optional per-dimension screen
   (arrow to a dimension → set level or `off`); rookie flow unchanged if skipped.
3. Manager TUI: show the resolving dimension level per rule and mark per-rule
   overrides as the top precedence layer.
4. Named difficulty sets under `config/profiles/` extended from all-equal
   templates (ADR-026) to per-dimension vectors.
5. ADR-037 briefing renders per-dimension state (already compatible; add the
   "off — covered elsewhere" line explicitly).
