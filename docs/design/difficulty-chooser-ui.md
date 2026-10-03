# Design: the difficulty chooser — one state, three views (`level` · `settings` · `manager`)

**Status:** design (2026-10-02). Underlying decisions: ADR-038 (per-dimension
difficulty vector), ADR-039 (rule ility-alignment + OR/max resolution),
ADR-026 (maturity levels), ADR-011 (global→repo cascade), ADR-009 (data not code).
Existing view docs: `doom-level-select.md` (the `level` view),
`keur-manager-tree-tui.md` (the `manager` view). This doc adds the middle view
(`settings`) and specifies how all three compose over **one** canonical state.

## The one-sentence model

There is **one canonical state** (the difficulty vector + per-rule overrides, in
`keur.toml`), **one mapping** from that state to which rules run
(ADR-039 `active()`), and **three views** that are just different lenses on that
same state. No view has its own storage; they read and write the one place, so
they can never drift. Everything is compatible because everything resolves
through the single mapping.

```text
   DOOM logo (braille — doom-level-select.md)

   [ keur level ]   [ keur settings ]   [ keur manager ]      ← Tab toggles the lens

        COARSE              MEDIOR               EXPERT
   one knob, 5 levels   level per ility      per-rule on/off
          │                   │                     │
          └──────────┬────────┴──────────┬──────────┘
                     ▼                    ▼
         keur.toml [difficulty]   keur.toml [difficulty.rules]
                     └──────────┬──────────┘
                                ▼
                  ADR-039 active(rule, vector)   ← the ONE mapping
                                ▼
                     gate reads active/action → PASS/WARN/BLOCK
```

## Canonical state (the single source of truth)

```toml
# keur.toml  (global at ~/.config, repo at <repo>/.config/.keur.toml)
[difficulty]                 # the vector — the ONLY difficulty storage
security     = 4
hardening    = 4
pii          = 5
quality      = 3
traceability = "off"
reliability  = 3
inclusivity  = 3

[difficulty.rules]           # expert per-rule overrides, same file
KEUR-STYLE-012 = "off"
KEUR-SQLI-003  = "on"
```

- The **vector** is canonical. A coarse "level N" is the special case where every
  ility equals N (ADR-038 §2). `keur level 3` is sugar for "write all ilities = 3".
- `off` is first-class (a dimension deliberately not gated here, ADR-038 §2);
  it is excluded from the average and dropped from the OR/max (ADR-039 §3).
- The ility set is data (ADR-039 §1): the views render whatever ilities the loaded
  corpus declares, so adding an ility never needs a view change.

## View 1 — `keur level` (coarse, exists: doom-level-select.md)

The DOOM "Choose your Difficulty" screen. Five levels, one knob, applied to the
whole vector at once. A `*` after the level means the on-disk vector is **not**
all-equal (someone tuned an ility in `settings`) or a per-rule override exists —
so "you are on level 3, but with deviations".

```text
                    Choose your Difficulty:

              I'm Too Young to KEUR
            > Hey, Not Too Sure
              KEUR Me Plenty            3 *      ← * = deviations exist below
              Ultra Compliance
              H4x0r c0d3fl3x

     Level 3 — Defined. Standard KEUR experience.
     * tuned: security=4, pii=5, traceability=off  (press Tab → settings)
```

Dragging the knob to a level **resets the whole vector to all-equal** (with a
confirm when it would overwrite tuned ilities — the Civ/XCOM "preset overwrites
your tweaks" prompt). This is the rookie flow; it never has to see the vector.

## View 2 — `keur settings` (medior, NEW — this doc)

The per-ility sliders. Collapsed, it shows one number: the **average** of the
non-`off` ilities (the "overall difficulty"). Expanded, each ility is a slider
`off · 1 · 2 · 3 · 4 · 5` (off is the leftmost stop, visually detached — it is
not level 0, it means "not gated here", ADR-039 §3).

```text
   ── keur settings ─────────────────────────────────
     Overall  ███████░░░  3.4 *          [ Enter: expand ▾ ]

   ── expanded ──────────────────────────────────────
     Overall  ███████░░░  3.4 *          [ Enter: collapse ▴ ]
       security      ████████░░  4   Security (25010)
       hardening     ████████░░  4   Security
       pii           ██████████  5   Security / data
       quality       ██████░░░░  3   Maintainability
       reliability   ██████░░░░  3   Reliability
       traceability  ·░░░░░░░░░  off  covered elsewhere
       inclusivity   ██████░░░░  3   Usability          *
```

Markers:
- `*` on **Overall** / the `level` label: the vector is not all-equal, or a
  per-rule override exists.
- `*` on an **ility**: at least one rule in that ility has a per-rule override
  (set in `manager`) that deviates from what the slider level alone would decide.
  So the row honestly says "this slider is 3, but there is hand-tuning under it".

Average rules: computed over non-`off` ilities only (an `off` dimension is not
"zero", it is excluded), so turning traceability off does not drag the overall
down for something deliberately not gated.

Editing a slider writes that one key in `[difficulty]`. Nothing else moves.

## View 3 — `keur manager` (expert, exists: keur-manager-tree-tui.md)

The per-rule tree. On top of the vector, a rule can be forced on/off regardless
of its ility's level. The tree shows, per rule, the resolving ility level and
whether a per-rule override is active (the top precedence layer, ADR-039 §6).
This is where a user kills "one or two rules I don't like" without touching the
sliders. Those overrides land in `[difficulty.rules]` and are what raise the `*`
on the owning ility in `settings`.

## Precedence (how the three compose)

Later wins, exactly ADR-038 §3 / ADR-039 §6:

```text
   base level (keur level)         → writes all ilities equal
     ↓ overridden by
   per-ility vector (keur settings) → sets individual ilities / off
     ↓ overridden by
   per-rule override (keur manager) → forces a specific rule on/off
```

The gate reads only the resolved result of ADR-039 `active()` with the per-rule
override applied last. The views show that same resolution — one state, one
mapping, three lenses.

## Global + repo cascade (ADR-011)

Two files, merged per key (repo wins; unset keys inherit global):

```toml
# ~/.config/keur.toml          GLOBAL — personal baseline
[difficulty]                   # "I run doom level 3 everywhere"
security = 3
hardening = 3
quality = 3
# … all 3

# <repo>/.config/.keur.toml    REPO — this repo deviates
[difficulty]
security     = 4               # public repo, stricter
traceability = "off"           # covered by another control
[difficulty.rules]
KEUR-STYLE-012 = "off"         # one rule I don't want here
```

Resolution: `repo ⊕ global` per ility, then ADR-039 `active()`. Any ility the repo
does not name inherits the global value. The `*` is cascade-aware: a repo that
deviates from global shows `*`, so "this repo is not just my global" is visible.

## Backup / restore (compatible with the existing manager backup)

The manager TUI already writes a **single-slot** `keur.toml.bak` before
overwriting a scope's config (keur-manager-tree-tui.md, "Save keeps one backup"),
and a change-journal / undo is its planned Layer 2. The difficulty views **reuse
that mechanism** — they do not invent a second history system:

- Any view, on save, writes the current `keur.toml` to `keur.toml.bak` (single
  slot) before writing the new state. One-step undo = restore `.bak`.
- Restore is non-destructive: restoring `.bak` first backs up the *current*
  state, so restore is itself undoable.
- Because there is one canonical state per scope, one backup per scope restores
  **all three views** at once — you cannot restore `level` and leave `settings`
  stale, since they are the same file. This is the "one mapping, everything
  compatible" guarantee applied to backup.
- The richer multi-step history is deferred to the manager's planned Layer 2
  change-journal, so difficulty and manager share one undo story when it lands.

## Why this is coherent

- **One state**: `[difficulty]` + `[difficulty.rules]` per scope. No per-view
  storage, so no drift.
- **One mapping**: ADR-039 `active(rule, vector)` is the only path to "which rules
  run". The gate and all three views read it.
- **Three views**: coarse / medior / expert, each a strict refinement of the one
  before (ADR-038 §3), composing by precedence, not competing.
- **Everyone served**: rookie uses one knob; medior tunes a few ilities; expert
  kills individual rules; a personal global + per-repo deviations give "something
  for everyone" without the views ever disagreeing.

## Build order (tracked separately)

1. ADR-039 backfill: every rule gets `ilities` + `maturity` (scripts/).
2. Self-check rule for unaligned rules.
3. `active(rule, vector)` OR/max in the ruleset layer + unit tests.
4. `[difficulty]` + `[difficulty.rules]` parsing, global→repo merge.
5. `keur settings` view (sliders, average, `*`), Tab-toggle wiring the three views.
6. `*`-markers in `level` and `manager`; backup/restore reuse.
