# Design: difficulty resolution — the one storage + mapping at the app's heart

@id DESIGN-DIFFRES

**Status:** design (2026-10-03). This is the technical contract that makes
ADR-038 (per-dimension difficulty vector) and ADR-039 (rule ility-alignment)
*implementable*. It specifies the single canonical storage, the cascade merge,
and the one resolution function that every consumer reads. It is the heart of the
app: the three views (level/settings/manager) and the gate all depend on it.

**@implements ADR-038** · **@implements ADR-039**
**@see** docs/design/difficulty-chooser-ui.md (the UI lens over this storage)
**@see** docs/adr/ADR-011-project-profile.md (the global→repo cascade this reuses)
**@see** docs/adr/ADR-026-rule-maturity-levels.md (the maturity band + existing `[policy] level`)
**@see** docs/adr/ADR-010-traceability.md (why this doc carries @id/@implements)

## 1. The data flow (one path, four consumers) @id DESIGN-DIFFRES.flow

There is ONE canonical storage and ONE mapping. The gate and the three views are
consumers of the same resolved state — they never compute strictness their own
way.

```text
  ~/.config/keur.toml            <repo>/.config/.keur.toml
   [difficulty] (global)          [difficulty] (repo override)
          │                              │
          └──────────── merge ──────────┘        @id DESIGN-DIFFRES.merge
                         │
                         ▼
                 DifficultyVector                 (ADR-039 struct)
                         │
            rule_active(rule, vector)             @implements ADR-039 (resolve)
                         │
             active / inactive  ──►  action (block|warn|advisory)
                         │
         ┌───────────────┼───────────────┬────────────────┐
         ▼               ▼               ▼                ▼
   keur level       keur settings    keur manager       the gate
   (shows avg)      (per-ility)      (per-rule)       (PASS/WARN/BLOCK)
```

Rule: no consumer reads the raw TOML or re-derives active-state. They all call
the resolver. This is the "one mapping, everything compatible" guarantee.

## 2. Canonical TOML schema @id DESIGN-DIFFRES.schema

The vector is the ILITY level map. Ilities are ISO 25010 sub-characteristics
(ADR-039 §1). `off` is a string; levels are integers 1..5.

```toml
[difficulty]                 # the canonical vector — the ONLY difficulty storage
confidentiality = 4
integrity       = 4
authenticity    = 3
analysability   = 3
modifiability   = 2
accessibility   = "off"      # deliberately not gated here (ADR-038 §2)
adaptability    = 3

[difficulty.rules]           # expert per-rule overrides (ADR-039 §6)
KEUR-STYLE-012 = "off"
KEUR-SQLI-003  = "on"
```

- An ility absent from `[difficulty]` inherits `default_level` (fresh = 1, CMMI-1).
- `off` ⇒ level 0 ⇒ the ility does not contribute to OR/max (ADR-039 §3).
- `[difficulty.rules]` is `on|off` per rule id; it wins over the vector result.

## 3. Backward-compat with `[policy] level` @id DESIGN-DIFFRES.compat

`[policy] level = N` already exists and drives `filter_by_maturity`
(config/ruleset.cpp). **DECISION A (proposed):** keep both; `level = N` is the
all-equal shorthand — it means "every ility = N". Resolution precedence:

```text
default_level (1)
  → [policy] level = N        (all ilities := N, the rookie knob)
    → [difficulty] per ility  (the medior overrides)
      → [difficulty.rules]    (the expert per-rule override)
later wins
```

So the old single knob becomes the special case where the vector is all-equal.
Nothing breaks: a repo with only `[policy] level = 3` resolves exactly as before.
A repo that adds `[difficulty]` refines it. `filter_by_maturity` is reimplemented
on top of `rule_active` with an all-equal vector, so there is one code path, not
two (ADR-014). @id DESIGN-DIFFRES.compat.one-path

## 4. Cascade merge (global → repo) @id DESIGN-DIFFRES.cascade

Same spirit as ADR-011. Two files, merged per key:

- Start from `default_level` for every ility.
- Apply global `[policy] level` (all-equal), then global `[difficulty]` per key.
- Apply repo `[policy] level`, then repo `[difficulty]` per key.
- Merge `[difficulty.rules]`: repo entries override global entries per rule id.

An ility the repo does not name inherits the global value; an ility neither names
inherits `default_level`. Repo always wins per key (never a blanket repo-replaces-
global). @id DESIGN-DIFFRES.cascade.per-key

## 5. The resolver contract @id DESIGN-DIFFRES.resolve

Already built (src/rules/rule/resolve.cpp, @implements ADR-039):

```cpp
int  DifficultyVector::level_of(ility)             // explicit or default_level; 0 = off
std::vector<std::string> rule_ilities(const Rule&) // explicit, else id-override, else category
bool rule_active(const Rule&, const DifficultyVector&)  // OR/max, off drops out, maturity 0→3
```

What this doc ADDS (not yet built): the loader that turns the two TOML files into
a `DifficultyVector`, and wiring `rule_active` into the scan/check pipeline in
place of the scalar `filter_by_maturity`. @id DESIGN-DIFFRES.loader

## 6. The group → ility → controls → rules chain @id DESIGN-DIFFRES.tree

The UI (difficulty-chooser-ui.md) renders a tree; this is where each level comes
from:

```text
ISO 25010 characteristic   (group — expandable)      e.g. Security
  └─ ility (sub-char)       (the SLIDER, 1..5/off)     e.g. integrity   = 4
       └─ controls          (rule categories)          sqli, cmdi, iac, …
            └─ rules         (live in keur manager)     KEUR-SQLI-002, …
```

- A **group** level is the average of its non-off ilities (DECISION B).
- Dragging a group sets all its ilities equal; deviating marks it `*`.
- Controls are the `category` values mapped to that ility (ADR-039 §1 table).
- Rules are NOT edited here — that is `keur manager` (ADR-039 §6).

## 7. Average + `*` marker — computed once @id DESIGN-DIFFRES.average

**DECISION B (proposed):** the average and the `*` deviation marker are computed
in the config/ruleset layer, not per view, so level/settings/manager show the
same number. Average ignores `off` ilities. `*` on a group/level means "the
vector under it is not all-equal, or a per-rule override exists".

## 8. Validation / failure behaviour @id DESIGN-DIFFRES.validate

**DECISION C (proposed):** be lenient at resolve time, strict via self-check.
- An unknown ility name in `[difficulty]` → ignored at resolve (does not match any
  rule) + a warning; the self-check rule (ADR-039 §4) flags it so it is visible.
- A rule whose ility is in no group → still resolvable (its ility just is not in
  any slider UI); the self-check flags it for alignment.
- An empty `[difficulty]` → every ility falls back to `default_level`.
- A malformed level (not 1..5 or `off`) → clamp to range + warning.
Rationale: the gate must never hard-fail on a difficulty-config typo (that would
train people toward `--no-verify`); make the problem visible, not blocking.

## 9. Open decisions (confirm before build) @id DESIGN-DIFFRES.decisions

- **A** — keep `[policy] level` as all-equal shorthand alongside `[difficulty]`
  (proposed: yes). Alternative: migrate `level` into `[difficulty]` and drop it.
- **B** — average + `*` computed in the config layer, one source (proposed: yes).
- **C** — lenient resolve + strict self-check for bad ilities (proposed: yes).

## 11. Performance: resolve-once, filter-ahead @id DESIGN-DIFFRES.perf

The vector and the two files cost nothing during a scan, because resolution
happens ONCE before any file is read — the same point where `filter_by_maturity`
already filters today (config/ruleset.cpp resolve_ruleset).

- Merge the two TOML files once → one `DifficultyVector` (a handful of string
  compares).
- Filter the 1083 rules once → the active set (one OR/max per rule, microseconds).
- The scan then runs only the active set. At CMMI-1 that is ~104 rules, not 1083,
  so a low level makes the scan FASTER, not slower.

Guarantee: `rule_active` is never called per-file or per-line; it is called once
per rule at ruleset-resolve time. No matrix is materialised — it is one vector
plus a per-rule override list, folded by the cascade stack (§4), not a 2-D grid.

## 12. Explain: forward + backward @id DESIGN-DIFFRES.explain

A deterministic explainer so overrides are testable and the question "what do I
change to get result X?" is answerable (extends ADR-021 debug-trace, ADR-035
self-explaining). Depends on the loader (§5) + resolver, so it is built AFTER
wiring and BEFORE the GUI — it is the verification instrument for steps 1–2.

**Forward** — `keur difficulty --explain`: show the cascade stack (which layer set
which ility), the resolved vector, and the active/sleeping counts.

```text
layer stack (later wins):
  default            all = 1
  global [policy]    level = 3        → all = 3
  repo  [difficulty] integrity = 4    → integrity 3→4
                     accessibility=off→ accessibility 3→off
  repo  [rules]      KEUR-SQLI-003=off
resolved: confidentiality 3  integrity 4  …  accessibility OFF
active: 487 / 1083   (sleeping 596: accessibility 140 off, maturity>level rest)
```

**Backward** — `keur difficulty --why <RULE>` / `--why-not <RULE>`: per rule, why
it is (in)active and WHAT TO CHANGE to flip it.

```text
CLI-A11Y-005 → INACTIVE
  ilities [accessibility]  maturity 2
  accessibility is OFF → inactive
  to ACTIVATE: set accessibility >= 2  (keur settings → accessibility 2)
```

Both forms get a `--json` variant so an e2e test can load a known keur.toml and
assert the resolved vector + active count — proving overrides work without the
GUI. @id DESIGN-DIFFRES.explain.json

Command shape: `keur difficulty --explain [--why RULE] [--json]` (lives where the
vector lives). @id DESIGN-DIFFRES.explain.cmd

## 10. Build order this doc enables @id DESIGN-DIFFRES.plan

1. Loader: `(global.toml, repo.toml) → DifficultyVector` (§2, §4) + unit tests.
   @id DESIGN-DIFFRES.step.loader
2. Wire `rule_active` into the pipeline, reimplement `filter_by_maturity` on it
   (§3). Verify a hand-written `[difficulty]` changes which rules run.
   @id DESIGN-DIFFRES.step.wire
3. Explain (§12): `keur difficulty --explain [--why RULE] [--json]` — forward +
   backward, the verification instrument for steps 1–2.
   @id DESIGN-DIFFRES.step.explain
4. Average/`*` helper in the config layer (§7).
   @id DESIGN-DIFFRES.step.average
5. The expandable GUI (difficulty-chooser-ui.md), reading/writing this storage.
   @id DESIGN-DIFFRES.step.gui

The system is usable after step 2 (edit keur.toml by hand → right rules run); the
explainer (step 3) proves it; the GUI (step 5) only makes it comfortable.
