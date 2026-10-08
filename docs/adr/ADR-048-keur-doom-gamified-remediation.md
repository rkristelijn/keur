# ADR-048 — `keur doom`: gamified remediation (findings are monsters, fixing is shooting)

@id ADR-048

**Status**: Proposed · **Date**: 2026-10-07 · **Coverage**: none · **Relates to**: ADR-026 (rule maturity levels — the DOOM skills already map onto them), `docs/design/doom-level-select.md` (the DOOM difficulty chooser, proven pipeline — this reuses its theme and braille/ASCII rendering), ADR-002 (single binary — why no external game engine/WAD), ADR-035 (keur:ignore — the suppression paths: inline/rule/disable) · ADR-046 (.keurignore value/allow — value-ignore), ADR-005 (platform abstraction — raw terminal input)

## Context

keur already dresses its difficulty chooser as DOOM's "Choose Skill Level"
(`doom-level-select.md`), mapping the 5 skills onto the 5 CMMI rule-maturity
levels (ADR-026). The theme is there; the engagement is not. Clearing findings
is a chore list — a flat wall of warnings a developer skims and ignores.

Two observations make a game the right shape here, not a gimmick:

- **Findings are already a target list.** A scan yields {rule, severity,
  file:line, fix-hint}. 1102 of 1109 rules carry a `fix:` hint. That is an
  enemy roster with a weapon already attached.
- **A codebase is hard to learn.** A newcomer does not know the files. If the
  "level" IS the repository — rooms are files/dirs, enemies are findings in
  them — then playing the level is walking the codebase. You learn the layout
  by clearing it.

The missing piece is that shooting must *do* something real: today the `fix:`
field is advice a human applies by hand. For a game to be honest ("kill" = the
finding is gone), shooting has to trigger an actual fix where one is mechanical.

## Decision

Add a hidden subcommand **`keur doom`**: a terminal ASCII game whose level is
the user's own repository, whose enemies are its findings, and whose weapon is
remediation.

### The level is the codebase, visualised
- The repo is rendered as a map: directories are rooms, files are tiles/doors,
  reusing the braille/ASCII rendering already proven in `doom-level-select.md`.
- Walking the map walks the tree — you SEE where the code lives. Clearing a room
  means you have read and fixed a file. Learning the codebase is the side effect
  of playing, which is the point.

### Traceability is the map; accessibility sets the playability
- The traceability links (ADR-047: `@implements` / `@proves` / `@shows` /
  `@explains` / `@see`) are the **doors between rooms**. A finding's room connects
  to the ADR it relates to, the test that proves it, the demo that shows it.
  Well-referenced code is navigable: you move from an enemy to its context and on
  to the next room. Code with no references is a set of isolated cells — you
  cannot route through it, so the game is short and dead-ended.
- So the game is a **mirror of the code's quality**: inaccessible, unreferenced
  code plays as a cramped, frustrating level; code with clean references and
  structure plays as a rich, connected one you can actually explore. This flips
  the incentive the right way — the game rewards exactly the traceability and
  structure keur already wants to enforce, instead of nagging for it. Playing
  well and writing accessible code become the same thing.

### The level is any graph keur can walk, not only source
Source files are the first level TYPE, not the only one. Anything keur already
scans and can turn into a navigable graph is a playable level — same loop (walk,
find, fix, re-scan), different map:
- **CI/CD pipeline**: stages are rooms, job dependencies are doors; enemies are
  pipeline findings (no timeout, no cache, allow_failure). You walk your pipeline.
- **IaC / Terragrunt state**: modules/resources are rooms, `dependency` blocks
  and state references are doors; enemies are the K8S/SLS/IaC findings. You
  navigate your infrastructure graph the way you navigate code.
- **Documentation**: docs are rooms, links/`@see` are doors; enemies are doc
  findings (broken link, orphan, missing section). You walk the doc graph.
- **Dead-code hunt**: a special mode — rooms with no inbound door (nothing
  references them) are the quarry. Finding unreferenced code IS the objective;
  the missing door is the signal — the traceability-as-map idea turned into a
  search.
The engine stays one thing: build a graph (nodes + reference edges) from what
keur scanned, place findings as enemies, let the player walk and clear it. The
domain only changes what a "room" and a "door" are.

### Enemies are findings, graded by severity
- Each finding spawns an enemy in the room of its `file`. Severity picks the
  sprite/threat: `warn` = imp, `block` = baron, `critical` = cyberdemon.
- The enemy carries its finding: rule id, message, and the `fix:` hint. Shooting
  it opens that context (so you learn WHAT the problem is, in place).

### Shooting triggers an autofix where one is mechanical
- Shooting runs an **autofix** for that finding if the fix is deterministic
  (formatting, a renamed import, a known-safe substitution, a style/consistency
  correction). The kill is confirmed by a **re-scan**: the enemy dies only when
  the finding is actually gone. No fake kills — the game cannot lie, because the
  verdict is keur's own scanner.
- Where no safe autofix exists, shooting opens the `fix:` hint and the file at
  `file:line`; the enemy dies when the user fixes it and the re-scan clears it.
- A finding you judge a false positive is a **pacifist retire** — not a fix, not
  a kill, but a deliberate choice of HOW to suppress it, and the choice has
  different reach (ADR-035 / ADR-046). The game surfaces the same four moves keur
  already has, as distinct actions with distinct cost:
  - **Line ignore** — inline `keur:ignore <RULE-ID>` on that one line. Narrowest:
    this enemy, this spot, visible in the diff. The "sidestep one imp" move.
  - **Rule ignore (repo)** — `.keurignore <RULE-ID>`. Retires every enemy of that
    rule in this repo. Powerful: it clears a whole monster CLASS at once — so it
    should feel like a big, deliberate lever, not a reflex.
  - **Value ignore** — `.keurignore secret <value>` / `allow <term>` (ADR-046).
    Retires every enemy carrying that exact value anywhere (the spellchecker
    dictionary). Kills by content, not location — a different weapon.
  - **Disable (global)** — `keur.toml [rules] disabled`. Turns the rule off
    everywhere, every repo. The nuclear option; the game should treat it as such
    (a confirm, a visible scar), because it stops the rule spawning at all.
- Anti-cheese: a retire is **recorded, never a kill** — a repo cannot be "won" by
  suppressing everything, and the HUD shows kills vs retires separately so a
  pacifist run reads as what it is. The broader the ignore (line < rule < value <
  disable), the louder the game flags it, mirroring its real blast radius.

### Cheat codes are the suppress / navigate verbs
The actions are entered as DOOM cheat codes — memorable, on-theme, and the cheat's
reach mirrors the real suppression reach (narrow god-mode → broad disable), so the
code teaches the blast radius:

| Cheat | DOOM meaning | keur action | Reach |
|-------|--------------|-------------|-------|
| `IDDQD` | god mode | **line ignore** — `keur:ignore <RULE-ID>` inline | one line (narrowest) |
| `IDKFA` | all weapons + keys + ammo | **rule ignore (repo)** — `.keurignore <RULE-ID>` | a whole rule class, this repo |
| `IDFA` | all but keys | **value ignore** — `.keurignore allow <value>` (ADR-046) | that value, anywhere (by content) |
| `IDCLEV` | warp past a level | **disable (global)** — `keur.toml [rules] disabled` | the rule, every repo (nuclear) |
| `IDCLIP` | noclip through walls | **fix a missing reference** — a room with no door is a wall where a door belongs; IDCLIP forces it by adding the missing `@see`/`@implements` link, reconnecting the room | one edge (restores navigability) |
| `IDDT` | automap reveal | **reveal mode** — show the whole graph / all findings at once | the overview |

`IDCLIP` is the odd one out and the nicest: it is not a suppress, it is a *fix*
of the traceability itself — the dead-code / missing-link case (a room with no
inbound door) turned into an action. You noclip to the orphaned room and lay the
door (the reference) that should have been there. (Trivia: noclip is `IDCLIP` in
DOOM II; the original 1993 DOOM used `IDSPISPOPD`.)
### Levels are maturity levels (ADR-026)
- Level N shows only maturity-N findings. Clear the room → the next maturity tier
  unlocks (the stricter rules spawn their enemies). You raise the codebase
  through the CMMI levels by playing, exactly the progression ADR-026 defines and
  the DOOM chooser already names ("I'm Too Young to KEUR" → "H4x0r c0d3fl3x").

### Honest scope: the autofix is the hard part, graded
Autofix must be SAFE or it is worse than the finding. So it is tiered, and only
the safe tier ships first:
- **Tier A (ships first)**: idempotent, verifiable text fixes — formatting,
  trailing whitespace, a missing header/marker (DOC-014), a renamed-API swap,
  a path-breakout (QUAL-079) rewritten to a relative base. Each is a pure
  text transform the re-scan confirms.
- **Tier B (later)**: structural fixes needing a parser (extract a long
  function, split a file). Out of scope initially.
- **Never auto**: anything semantic (does this `catch` really swallow an error?)
  — those stay "open the hint, human fixes", the enemy waits.

## Why not the alternatives

- **Port `doom-ascii` (the real DOOM)**: tens of thousands of lines of C + a WAD
  asset, GPL, and a game that has nothing to do with the repo. It would bloat the
  binary (ADR-002) and teach the player nothing about their code. We take the
  *theme* (already proven), not a DOOM engine.
- **A pure easter egg (splash screen, no gameplay)**: fun once, then dead. The
  value is the remediation + codebase-learning loop, not the nostalgia.
- **Shoot = instantly mark fixed (no re-scan)**: lets the game lie; a cleared
  repo would not actually be clean. The re-scan confirm is non-negotiable.
- **First-person raycaster vs top-down map**: top-down maps the tree legibly
  (rooms = dirs) and is cheaper to render in ASCII; first-person is more DOOM but
  obscures the "this is your codebase" readability. Lean top-down; revisit if the
  first-person view adds to the learning goal. (Open question.)

## Scope & limits
- **In scope**: `keur doom` subcommand; repo→map rendering (reuse braille/ASCII);
  findings→enemies; shoot→Tier-A autofix with re-scan confirm; pacifist retire
  via the four ignore moves (line / rule / value / disable, ADR-035/046);
  maturity-as-levels (ADR-026).
- **Out of scope (follow-ups)**: level TYPES beyond source (CI/CD pipeline, IaC/
  Terragrunt state, documentation, the dead-code hunt — same graph engine, a new
  map adapter each); Tier-B structural autofix; first-person view;
  score/leaderboard persistence; multiplayer/anything networked.
- **Single binary (ADR-002)**: all self-written C++, no WAD, no external engine,
  no GPL code. Raw terminal input via the platform abstraction (ADR-005), like
  the existing TUIs (`keur manager`).

## Consequences
- **Positive**: remediation becomes a loop a developer wants to run, not a wall
  they skim. Clearing a level is clearing real findings, confirmed by the scanner.
- **Positive**: onboarding — a newcomer learns the codebase layout by playing it,
  and learns what each finding means in place (the enemy carries its hint).
- **Positive**: reuses what exists — the DOOM theme, maturity levels, the `fix:`
  hints on 1102 rules, `keur:ignore`.
- **Positive**: the game is a mirror of code quality. Because the traceability
  links are the doors, accessible well-referenced code plays rich and long while
  unreferenced code plays short and dead-ended — so the fun aligns with the
  traceability keur wants (ADR-047), rather than fighting it.
- **Negative / caution**: autofix safety is the whole risk. A wrong auto-rewrite
  is worse than the finding. Mitigated by the SAFE/structural/never tiers, pure
  text transforms first, and the re-scan confirm (plus git — the user can revert
  a room).
- **Negative / caution**: it must never become keur's identity. keur is a
  security gate; `doom` is a hidden mode that makes the gate's output actionable
  and teachable, not the product.
