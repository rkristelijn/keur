# Design: `keur manager` — interactive tree TUI for hooks & rules

**Status:** partially implemented — Layers 4–6 built (interactive rules tree).
**Raised:** 2026-09-21
**Implemented:** 2026-09-22 (platform input, pure model/view, interactive loop,
read-only Defaults baseline branch, save-with-one-backup)

Built so far:
- **Layer 4 — platform input:** `platform::RawModeGuard` + `read_key()` +
  pure `decode_key()` (`common/keyinput.cpp`, @see KEUR-KEYINPUT). Arrows/hjkl,
  Space, Enter, `s`, `q`, `g`/`G`. POSIX termios (IXON off so `s` is a normal
  key) and Win32 `ENABLE_VIRTUAL_TERMINAL_INPUT` deliver the same VT sequences.
- **Layer 5 — pure model + view:** `manager/model.*` (@see KEUR-MANAGER-MODEL)
  is the Scope→Category→Rule tree with pure reducers (`move`, `expand`,
  `collapse`, `toggle_focused_rule`) producing a `ChangeSet` of `{scope, rule_id,
  disable}` edits by diffing against the build-time baseline. `manager/view.*`
  (@see KEUR-MANAGER-VIEW) renders `(model, opts) → lines`, colour via SGR,
  `NO_COLOR`/`--no-color` honoured. Both fully unit-tested with no terminal.
- **Layer 6 — interactive loop:** bare `keur manager` enters raw mode, renders,
  reads keys, applies reducers; `s` saves every pending edit through the SAME
  writer the CLI uses (`cmd::rules_toggle`) — one write path, no CLI/TUI drift;
  `q` prompts when dirty. Falls back to a helpful message (not a crash) when
  there is no tty. `keur manager --status [--json]` stays the read-only board.
- **Read-only Defaults branch:** the tree's first root is `Defaults` — the
  shipped template's Scanners + Policy settings and the **full rule catalogue**
  as the baseline (every rule shown "on"), all read-only. `Global` and
  `This repo` layer their editable overrides on top.
- **Save keeps one backup:** before overwriting a scope's `keur.toml`, its
  current content is copied to `keur.toml.bak` (single slot), so the previous
  version is always recoverable.

Not yet built (later layers): change journal / undo / stash (Layer 2 below),
editable scanner + policy settings, custom-scripts nodes, the `/` filter, and
the full headless CLI parity surface (`config set`, `scanner register`, …).

**Relates to:** `docs/design/keur-configure-repl.md` (the status-board REPL),
`cmd/hook.cpp` (hook state), `learn/monitor.cpp` (existing fullscreen render),
`render/box.*` (drawing primitives), ADR-003 (I/O via adapters),
ADR-005 (platform abstraction).

## What this is

A fullscreen, keyboard-driven terminal tree that shows keur's live
configuration and lets you toggle it:

- **Hooks** — global (`core.hooksPath`) and per-repo (`.git/hooks`), each shown
  as enabled / disabled with a checkbox and colour.
- **Rules** — grouped by category (security, secrets, quality, inclusivity,
  learned, …), each rule toggleable on/off.

You navigate with arrows or `hjkl`, expand/collapse nodes with `Enter`, and
flip a node on/off with `Space`. Colours and check/cross glyphs make the
enabled/disabled state readable at a glance.

Command: **`keur manager`** (exits with `q`; see "Exit & save" for why `q`
and not `:wq`).

### How this differs from `keur configure`

`keur configure` (already designed) is a **status board + guided menu**: it
prints a read-only summary, then offers a short list of named actions
(install global hook, set compose mode, scaffold pipeline gate). It is
menu-driven and shallow.

`keur manager` is the **tree editor**: a navigable, collapsible view of every
hook and every rule, with inline toggles. It is the "power surface" for someone
who wants to see and flip individual items, where `configure` is the "guided
setup" surface.

They can coexist, or `manager` can absorb the board as its header. This doc
assumes they stay separate for now; `manager` links out to `configure`'s
actions where a guided flow is better (e.g. scaffolding a CI gate).

## The tree

The tree spans everything that governs a commit, grouped by **where it lives**
(global vs. this repo) and **what it is** (hooks, scripts, config, rules,
external scanners). Nodes are shown even when *not installed / not present*, so
the tree doubles as "what you could turn on", not just "what is on".

```
keur manager · /Users/remi/git/hub/keur                         [q] quit  [?] help

 ▼ Global  (~/.config)
   ▼ Hooks  (core.hooksPath → ~/.config/git/hooks)
     [x] pre-commit          installed
     [ ] pre-push            not installed          ← shown even if absent
   ▼ Scripts  (~/.config/keur/scripts)               (new: global check scripts)
     [ ] lint-shell.sh       present, not registered
   ▶ Config  (~/.config/keur/keur.toml)              [present]

 ▼ This repo  (/Users/remi/git/hub/keur)
   ▼ Hook managers  (detected — keur coexists, does not clobber)
     [x] keur            .git/hooks (or core.hooksPath)   active
     [ ] husky           .husky/                          detected, chained
     [ ] pre-commit      .pre-commit-config.yaml          detected, chained
     [ ] lefthook        lefthook.yml                     not present
     [ ] overcommit      .overcommit.yml                  not present
   ▼ Hooks  (.git/hooks)
     [ ] pre-commit          not installed  (global active)
     [ ] pre-push            not installed  (global active)
   ▼ Config  (./keur.toml)                            [absent]
   ▼ Rules  (312 total · 298 on · 14 off)
     ▼ security          (23 on · 2 off)
       [x] KEUR-SEC-001    private-key                  block
       [ ] KEUR-SAST-001   non-literal-regexp           warn      ← off
       …
     ▶ secrets           (81 on)
     ▶ custom-rules      [.keur/rules]  (5, editable)   (repo-local rules)
     ▶ supply-chain      [KEUR_RULES_DIRS: /team/shared]  (74, read-only)
   ▼ Scripts  (.keur/check.sh — project-local, ADR-019)
     [x] .keur/check.sh      registered  (tracked ✓)

 ▼ Scanners  (proven external tools — evidence sources, ADR-019)
   [x] gitleaks     secrets   block   on PATH ✓        cfg: .gitleaks.toml / .gitleaksignore
   [x] semgrep      sast      warn    on PATH ✓        cfg: .semgrep.yml / .semgrepignore
   [ ] trivy        deps      warn    NOT on PATH       cfg: trivy.yaml / .trivyignore
   [ ] checkov      iac       warn    NOT on PATH       cfg: .checkov.yaml
   [ ] trufflehog   secrets   block   NOT on PATH
     …each scanner expands to show/edit its ignore + config files.

 ▶ Policy  (block-at 0.85 · warn-at 0.50 · ~/.config/keur/keur.toml)
```

- **Top-level split by scope:** `Global`, `This repo`, `Scanners`, `Policy`.
  Global and repo each hold their own Hooks / Scripts / Config (and repo holds
  Rules + detected hook managers). Mirrors how config/rules/hooks resolve in
  layers.
- **Absent things are shown, not hidden.** A global pre-push hook that is *not*
  installed still appears (unchecked), so `Space` installs it — the tree is the
  install surface too. Same for scanners not on PATH and a repo `keur.toml` that
  doesn't exist yet.
- **Scanners are first-class (ADR-019).** Each proven tool (gitleaks, semgrep,
  trivy, checkov, trufflehog, grype, osv-scanner, tfsec…) is a node: toggle it
  on/off for a tier, see PATH availability, and **expand it to see and edit its
  own ignore/config files** — `.gitleaksignore`, `.semgrepignore`, `.trivyignore`,
  `.checkov.yaml`, etc. keur orchestrates the battery; the manager is where you
  see and tune each tool without hunting for its dotfile.
- **Scripts (new):** global check scripts (`~/.config/keur/scripts`) and the
  repo's project-local `.keur/check.sh` (ADR-019), with tracked-status shown
  (the trust gate — an untracked script is inert).
- **Counts/columns** as before: `on · off` summaries, `action` + source on the
  right (`from KEUR_RULES_DIRS`, `read-only`, `on PATH ✓`).

## Coexisting with other hook managers (Husky, pre-commit, lefthook, …)

keur is not the only tool that wants `.git/hooks` / `core.hooksPath`. A repo may
already use **Husky** (`.husky/`, JS), **pre-commit** (`.pre-commit-config.yaml`,
Python), **lefthook** (`lefthook.yml`), **Overcommit** (`.overcommit.yml`, Ruby),
or a raw `core.hooksPath`. keur must **detect, encapsulate, and chain** — never
silently clobber someone's existing setup.

Design:

- **Detect, don't assume.** On load, the manager probes for each known manager by
  its signature file (`.husky/`, `.pre-commit-config.yaml`, `lefthook.yml`,
  `.overcommit.yml`) and by inspecting `core.hooksPath` and `.git/hooks`
  contents. Detected managers appear as nodes with their state.
- **Chain, don't replace (default).** keur's hook stub already has compose modes
  (`augment` / `replace` / `keur-only`, in `cmd/hook.cpp`). Extend the orchestrator
  so `augment` runs keur **and then delegates to the detected manager's runner**
  (e.g. `npx husky`… no — invoke the manager's own hook entry: run `.husky/pre-commit`,
  or `pre-commit run`, or `lefthook run pre-commit`). The manager keeps owning its
  checks; keur adds its gate in front. This is the "encapsulate" you asked for.
- **The manager reads the other tools' configs.** keur's pre-commit/pre-push
  path must *understand* the neighbouring config files enough to run and manage
  them: know that `.pre-commit-config.yaml` means "run `pre-commit`", that
  `.husky/pre-commit` is an executable to exec, that `lefthook.yml` means
  "`lefthook run`". The tree shows these as manageable nodes; toggling one sets
  how keur chains to it.
- **One safe takeover path.** If a repo wants keur to *own* hooks outright,
  `keur-only` mode is the explicit, opt-in choice — shown in the tree, never the
  default when another manager is detected.
- **Never edit their files blindly.** keur may *read* `.pre-commit-config.yaml` /
  `.husky/*` to display and to know how to chain, but it writes only its own
  artifacts (its stub, its `keur.toml`). Managing another manager's config is
  surfaced as "open/edit" in the tree, not silent rewrites.

This makes keur a **hook meta-manager**: the outer gate that composes with
whatever the repo already uses, so adopting keur never means ripping out Husky.

### MVP boundary (what the first shippable tree shows)

The full tree above is the target. To get there bottom-up without building
throwaway UI, the **MVP** is a read-only `keur manager --status` that prints:

1. **Config scopes** — already built (`keur config list`, layer 0).
2. **Hooks + detected managers** — global + repo, installed-or-not, plus which
   other hook managers are present (probe the signature files).
3. **Scanners** — the `[scanners.*]` inventory with PATH availability + which
   config/ignore files exist for each (stat only, no editing yet).
4. **Rules** — grouped by category with counts (all shown "on" until the
   disable-list layer lands).

That print *is* the tree's data model, exercised without a terminal. Toggling,
disable-state, scanner + manager chaining, and config-file editing hang off it
in later layers. Interactivity (raw-mode, keys) is the last layer.

## Keys

Modeless, single-keystroke. No vi command line.

| Key | Action |
| --- | --- |
| `↑` / `k` | move up |
| `↓` / `j` | move down |
| `←` / `h` | collapse node (or jump to parent if already collapsed) |
| `→` / `l` | expand node |
| `Enter` | toggle expand/collapse on a group; on a leaf, same as `Space` |
| `Space` | toggle the item on/off (checkbox) |
| `g` / `G` | jump to top / bottom |
| `/` | incremental filter (type to narrow rules by id/title; `Esc` clears) |
| `a` | toggle **all** children of the focused group at once |
| `s` | save changes now (also happens on quit) |
| `u` | undo last applied change (inverse ChangeSet) |
| `z` | stash the current edits; stash overlay lists/pops them |
| `q` | quit (prompts if there are unsaved changes) |
| `?` | toggle help overlay |

`h`/`l` doubling as collapse/expand *and* left/right matches how file-tree TUIs
behave (nnn, lf, the VS Code tree), so muscle memory transfers.

## Exit & save — why `q`, not `:wq`

You asked whether to save on `q`, or go vi-style with `Esc` + `:wq` / `:q`.
Recommendation: **modeless, `q` to quit, explicit `s` to save, and prompt on
quit if dirty.** Reasons:

- The tree is a **selection/toggle** surface, not a text buffer. vi's modes
  exist because you're inserting characters *and* issuing commands with the same
  keys; here every key is already a command, so there's no mode to switch out of
  and `:` buys nothing.
- `:wq` / `:q` require a command-line mode, a parser, and teaching the user two
  more concepts. That's cost with no matching benefit for a checkbox tree.
- The safest, least-surprising contract is the one editors like `htop`, `lazygit`
  and `k9s` use: **`q` quits; if there are unsaved edits, ask.** Destructive loss
  is prevented by the prompt, not by ceremony.

So:

- `s` — write changes to disk immediately, stay in the TUI.
- `q` — if clean, exit. If dirty, prompt: `Save changes? [y]es [n]o [c]ancel`.
- `Esc` — context key: closes the help overlay or clears the `/` filter; it does
  **not** quit (avoids the "did Esc save or discard?" ambiguity entirely).

If you later want a "discard and quit now" fast path, `Q` (shift-q) can mean
"quit without saving" — an explicit, hard-to-hit-by-accident key. That's the
only concession to power users; still no command line.

## Multiple scopes: many keur.toml, custom rules, custom scripts

keur config isn't one file — it resolves in layers (README "Configuration"):
built-in defaults → global `~/.config/keur/keur.toml` → `$KEUR_HOME/config` →
explicit `--config`. Plus rule dirs come from `$KEUR_HOME/rules` **and** every
`$KEUR_RULES_DIRS` entry. The manager must make this layering **visible and
editable**, not flatten it into one opaque view.

So the tree gains a **scope dimension**. Each toggleable item shows which layer
it comes from and which layer a change will write to:

```
 ▼ Config scopes
   [global]  ~/.config/keur/keur.toml           (block-at 0.85 · 12 disabled)
   [repo]    ./keur.toml                          (overrides global · 2 disabled)
   [explicit] --config ./ci/keur.toml            (active this run)
 ▼ Rules  (312 total)
   ▼ security            [global+repo]
     [x] KEUR-SEC-001    private-key       block   (base: rules/)
     [ ] KEUR-SAST-001   non-literal-regexp warn   (off in: repo keur.toml)   ← off
   ▶ custom-rules        [KEUR_RULES_DIRS: ~/my-keur-rules]  (5 rules, editable)
   ▶ supply-chain        [KEUR_RULES_DIRS: /team/shared]     (74, read-only)
 ▼ Custom scripts        (project-local scanners / adapters)
   [x] ./ .keur/scripts/lint-terraform.sh        registered
   [ ] ./ .keur/scripts/check-licenses.sh        present, not registered
```

Rules:

- **A toggle writes to a chosen scope.** Default = repo `keur.toml` (closest,
  reviewable). A modifier key targets global. The row shows where the *effective*
  state currently comes from, so you see when repo overrides global.
- **Read-only dirs are honoured.** A `KEUR_RULES_DIRS` entry you don't own
  (shared/vendored) can be toggled *off* (via the disable list in a scope you
  *do* own) but never edited/renamed on disk. The tree marks it `read-only`.
  This is exactly why the disable-list approach (below) beats renaming files.
- **Custom rules** live in a writable `KEUR_RULES_DIRS` entry (e.g.
  `~/my-keur-rules`); the manager can create/enable/disable these and shows them
  as an editable group.
- **Custom scripts** — project-local scanners/adapters (see
  ADR-019 external scanners / project-local adapter). The manager lists scripts
  discovered under a project `.keur/scripts/` (or wherever ADR-019 lands),
  showing registered-vs-present so you can wire one in with `Space`. Registration
  writes to the owning scope's `keur.toml` (a `[scanners]`/`[scripts]` entry).

Which scope a write lands in is always shown before it's applied (the save
prompt lists the files it will touch), so multi-file edits are never silent.

## CLI parity is a hard requirement, not a convenience

Everything the tree can do **must** have a non-interactive flag form, because an
AI agent (or CI, or a script) has to manage keur headlessly — no terminal, no
raw mode, no human. The TUI is a *view onto the same operations the CLI exposes*;
it must never be the only way to do something. Concretely:

```
# Rules — enable/disable, per scope
keur rules disable <id|category|glob> [--scope repo|global|--config <f>]
keur rules enable  <id|category|glob> [--scope ...]
keur rules list    [--scope ...] [--json]          # machine-readable state

# Hooks — already exist
keur hook --install | --global | --status | --remove [--global]

# Custom rule dirs & rules
keur rules add-dir <path> [--scope ...]            # register a KEUR_RULES_DIRS entry
keur rules new <id> --category <c> --dir <path>    # scaffold a .rule (optional)

# Custom scripts / scanners (ADR-019)
keur scanner register <path> [--scope ...]
keur scanner unregister <path> [--scope ...]
keur scanner list [--json]

# Config scopes
keur config list [--json]                          # show every resolved layer + source
keur config get  <key> [--scope ...]
keur config set  <key> <value> --scope repo|global # writes the right keur.toml

# Change lifecycle — save/undo/revert/stash (see "Changes are first-class")
keur config history [--scope ...] [--json]         # journalled changes, newest first
keur config undo    [--scope ...]                  # invert the last change
keur config revert  <change-id> [--scope ...]
keur config stash [list|pop|drop] ...              # park/restore a change set

# The tree itself, headless
keur manager --status [--json]                     # print the whole tree, no TUI
```

Design contract (same as `keur configure`): **the TUI calls these same
operations** — it does not have a private write path. `manager` builds its model
from `config list` / `rules list` / `scanner list`, and every `Space`/`s`
delegates to the `enable`/`disable`/`register`/`config set` primitives. That way:

- an AI can drive keur end-to-end with flags and `--json`, and
- the TUI and CLI can never drift, because they share the write functions
  (pure reducers produce a *change set*; one persister applies it, used by both).

`--json` on the read commands (`config list`, `rules list`, `scanner list`,
`manager --status`) is what makes keur **AI-manageable**: the agent reads state
as JSON, decides, and issues `enable`/`disable`/`set` commands. This mirrors how
`scan` already emits JSONL for machines.

## The hard part: what does "disabled" mean?

**keur has no disable concept today.** The engine loads every `.rule` it finds
under `$KEUR_HOME/rules` and each `$KEUR_RULES_DIRS` entry, and runs them all.
Toggling a rule "off" needs somewhere to persist that intent. Options, from
least to most invasive:

1. **Disable list in `keur.toml`** *(recommended).* A `[rules] disabled = [...]`
   array of rule ids (and/or `category:*` globs). The engine skips any rule
   whose id matches on load. This is:
   - non-destructive (the `.rule` files stay put),
   - reviewable (it's in the repo's config, diffable, PR-able),
   - already the natural home — `keur.toml` is where policy lives.
   The manager reads and writes this array. Hooks toggling writes the same file
   the `configure` design already targets (`[hooks] mode`).

2. **Per-repo overlay file** (`.keur/disabled`) separate from `keur.toml`. Keeps
   config clean but adds a second state file and a second thing to explain.

3. **Rename `.rule` → `.rule.off`** on disk. Simplest engine change (glob only
   `*.rule`) but *destructive-ish*: it mutates the rule corpus, which is bad for
   imported/vendored rule dirs you don't own (`KEUR_RULES_DIRS` pointing at a
   shared set — you can't rename files there).

Option 1 wins: it fits the existing config resolution, works for read-only rule
dirs, and every toggle is a reviewable line. **Open question for you:** should a
disable be per-repo (repo `keur.toml`) or global (`~/.config/keur/keur.toml`)?
The tree can show both scopes and let `Space` target the repo scope by default,
with a modifier (e.g. `S`) for global — but that's a v2 nicety.

Engine change required (small, ADR-009-friendly — rules are data): the loader
gains a "skip if id ∈ disabled set" filter. Everything else is unchanged. This
needs a `@proves` test and a note in `docs/rules-authoring.md`.

## Changes are first-class: save, revert, temporary, stash

A toggle isn't a one-off write — it's a **change** with a lifecycle. You need to
save it, undo it, park it, or make it expire. Treat every edit as a recorded
transaction (fits your change-management instincts: a change has intent, a
record, and is reversible).

### The unit of change

The `ChangeSet` (see Architecture) is already the transaction: an ordered list
of edits `{scope, key, old_value, new_value, reason?}`. Because it carries
`old_value`, **it is invertible** — undo is just applying the inverse ChangeSet.
Every applied ChangeSet is appended to a per-scope journal:

```
~/.config/keur/history/changes.jsonl        (global scope)
<repo>/.keur/history/changes.jsonl          (repo scope)
```

One JSON object per change: timestamp, scope, edits (with old/new), who/what
(`cli` | `tui` | `ai`), and optional `reason`. This mirrors how the learn loop
already journals every decision to `eval/history/optimiser.jsonl` — same shape,
same "append-only record of what changed and why".

### Save

- `s` in the TUI / implicit on quit: apply the pending ChangeSet, append to the
  journal. Nothing is written until save (edits accumulate in memory), so you can
  toggle freely and bail with no trace via `q`→discard.
- CLI writes (`rules disable`, `config set`) are atomic single-change saves —
  each is its own journalled transaction.

### Revert / previous versions

Two layers, because not every keur.toml lives in a repo:

1. **git, when the file is tracked.** A repo `keur.toml` is a plain text file;
   git *is* its version history. The manager shows this (`git log --oneline` for
   the file) and can diff/restore via the git adapter. No reinvention.
2. **keur's own journal, always.** The global `~/.config/keur/keur.toml` is
   usually *not* in a repo, so keur keeps its own history (above). Commands:

   ```
   keur config history [--scope ...] [--json]     # list changes, newest first
   keur config undo    [--scope ...]              # invert the last ChangeSet
   keur config revert  <change-id> [--scope ...]  # restore state as of a change
   ```

   `undo`/`revert` build an inverse ChangeSet from the recorded `old_value`s and
   run it through the same persister — so a revert is itself a journalled change
   (never a silent overwrite). In the TUI, `u` = undo last, and a history overlay
   lets you pick a point to revert to.

### Temporary disable (TTL / expiry)

Distinguish **permanent** from **temporary** off:

- **Permanent:** the id lands in `[rules] disabled` — reviewable, in git, stays
  until someone re-enables it.
- **Temporary:** `[rules] disabled_until` — a map of `id → ISO-8601 expiry` with
  a required `reason`. On load, keur drops entries whose expiry has passed
  (re-enabling the rule automatically) and can warn that a temp-disable lapsed.

  ```
  keur rules disable <id> --until 2026-10-01 --reason "flaky, tracking in TICKET-123"
  keur rules disable <id> --for 7d --reason "..."     # relative TTL
  ```

  In the TUI a temp-disabled rule shows its expiry (`off · until Oct 1`) in
  amber, so "temporarily off" never quietly becomes "off forever". This is the
  honest version of the `keur:ignore` inline suppression — but time-boxed and
  centrally visible.

- **Session-only:** for a "just this run, don't persist" toggle, an env override
  `KEUR_DISABLE=<id,id>` (parsed like `KEUR_RULES_DIRS`) skips rules for one
  invocation without touching any file. Useful for a quick local experiment or
  for an AI trying a what-if.

### Stash

Park a set of changes without committing them to the active config, then restore
later — leaning on the ChangeSet abstraction:

```
keur config stash [--scope ...] [--message "..."]   # snapshot current effective
                                                     #   config, revert to base
keur config stash list [--json]
keur config stash pop  [<stash-id>]                 # re-apply a stashed ChangeSet
keur config stash drop <stash-id>
```

A stash is a named ChangeSet stored under `history/stashes/`. `stash` records the
diff between current and a chosen baseline and reverts to the baseline; `pop`
re-applies it. This gives you "let me try running with all quality rules off,
then put my setup back" without hand-editing files. In the TUI: `z` stashes the
pending/edited state, and a stash overlay lists and pops them.

### AI-safe by construction

Because every change is a journalled, invertible ChangeSet reachable by flag,
an AI managing keur can: read state (`--json`), make a change, and — crucially —
**undo cleanly** if it made a mistake (`keur config undo`), or stash its
experiment before trying something. The audit trail (`who = ai`, `reason`) means
a human can always see what the agent changed and roll it back. Reversibility is
the safety net that makes handing config control to an AI acceptable.

## Architecture (fits existing layers)

Reuse what `learn/monitor.cpp` already proves works; add only input handling.

```
cmd/manager.cpp        orchestrator: build model → loop(render, read key, apply)
cmd/config.cpp         `keur config list/get/set` (+ --json) — CLI scope surface
cmd/scanner.cpp        `keur scanner register/unregister/list` (ADR-019)
cmd/rules.cpp          extend with enable/disable/add-dir (already has `list`)
manager/model.*        pure data: Tree of Nodes {kind, scope, label, state, children}
                       + pure reducers: toggle(node) → ChangeSet, expand, move
manager/view.*         pure render: Tree + viewport + cursor → vector<string>
                       (uses render/box, render/display_width; colour via SGR)
config/ops.*           THE shared operation layer: enable_rule / disable_rule /
                       register_scanner / set_config, each (scope, target) →
                       ChangeSet. ONE persister applies a ChangeSet via adapters.
                       Both the CLI commands AND the TUI call these. No private
                       write path anywhere else — this is what stops drift.
config/scopes.*        resolve the layer stack (defaults→global→KEUR_HOME→--config)
                       and, for a write, pick the target keur.toml + write it back.
config/history.*       append/read the per-scope change journal (changes.jsonl);
                       build inverse ChangeSets for undo/revert; stash store.
                       Pure over the journal contents; I/O via fs adapter.
platform additions     raw-mode enter/leave + read one key (see below)
```

The **ChangeSet + single persister** is the load-bearing idea: a toggle in the
TUI and a `keur rules disable X` on the CLI both produce the same `ChangeSet`
(a list of "set key K in scope S to V" edits) and hand it to the same persister.
The TUI never writes files directly; it can't drift from the CLI because there
is only one writer.

Key design rules this must honour (from AGENTS.md / ADRs):

- **I/O at the edge (ADR-003):** all reads (git config, dir listing, file reads)
  and writes (keur.toml) go through the `git`/`fs` adapters, so the model and
  reducers are pure and unit-testable with spoofed state — exactly like
  `cmd/hook.cpp` is today.
- **Pure model + reducers:** navigation and toggling are pure functions over the
  tree (`move`, `expand`, `collapse`, `toggle`, `toggle_all`). Tests characterise
  them without a terminal (`@proves`). The render function is pure too:
  `(tree, viewport) → lines`, testable by snapshotting the produced strings.
- **Platform abstraction (ADR-005):** the *only* new OS-divergent pieces are
  raw-mode and key-reading. They go in `platform.h` /
  `platform_posix.cpp` / `platform_win32.cpp`, never in business code. The
  `KEUR-PORT-*` self-check rules enforce this.

### New platform primitives (the missing piece)

`platform.h` currently has `terminal_size()` but no input. Add:

```cpp
/// Enter/leave "raw" terminal mode (no line buffering, no echo). RAII-friendly:
/// callers use a guard so the terminal is always restored, even on throw/exit.
/// posix: tcgetattr/tcsetattr(TCSANOW) with a saved termios
/// win32: GetConsoleMode/SetConsoleMode (disable line input + echo)
struct RawModeGuard { /* restores on destruction */ };
RawModeGuard enter_raw_mode();

/// Read one logical key press, blocking. Decodes escape sequences (arrows,
/// Home/End) into a stable enum so business code never parses ESC-[ itself.
enum class Key { Up, Down, Left, Right, Enter, Space, Char, Escape, Unknown, /*…*/ };
struct KeyEvent { Key key; char ch; };   // ch valid when key == Char
KeyEvent read_key();
```

This keeps all `\033[` / `termios` / `SetConsoleMode` knowledge behind the
platform wall, matching how the ANSI clear-screen is used in `monitor.cpp`.

### Render loop (mirrors monitor.cpp)

```
enter_raw_mode()  (RAII guard)
loop:
  draw_frame(model, cursor)        // "\033[2J\033[H" + view lines, like monitor
  ev = read_key()
  model = reduce(model, ev)        // pure
  if ev quits → break (prompt if dirty)
leave_raw_mode() via guard
persist(model) if saved
```

`draw_frame` reuses `render/box` and `display_width` so borders/columns line up
with wide/unicode content, and colours are emitted as SGR escapes (with a
`--color auto|always|never` flag echoing `scan`'s existing option).

## Colour & glyphs

- Enabled: green `[x]` (or `✔`), label in default weight.
- Disabled: dim/grey `[ ]` (or `✖`), label dimmed — a disabled `block` rule also
  gets a subtle warn tint, because that's the risky state.
- Focused row: reverse-video or a `❯` gutter marker.
- Group header colour keyed to health: all-on = green, some-off = yellow.
- Respect `NO_COLOR` and `--color never` (accessibility + pipe-safety).

## Non-interactive parity (same principle as `configure`)

The TUI is ergonomics only. Everything it does must be reachable by flags so CI
and scripts never depend on a terminal:

- `keur manager --status` — print the tree once, non-interactive, no raw mode
  (like `hook --status` / `configure --status`).
- `keur rules disable <id|category>` / `keur rules enable <id>` — the flag form
  of `Space`, writing the same `[rules] disabled` array. (New; small.)
- Hook toggles already exist as `keur hook --install/--global/--remove`.

## What to build, in order

Bottom-up: the shared ops layer + CLI first (usable by AI immediately, testable
without a terminal), the TUI last as a view on top.

1. **Config scopes + writer.** `config/scopes.*`: resolve the layer stack and
   pick/write the target `keur.toml`. `[rules] disabled` parsing + a loader
   skip-filter (honour a disable list). Tests + `docs/rules-authoring.md` note.
   *(Unblocks everything.)*
2. **Shared ops layer + journal.** `config/ops.*`: `enable_rule`/`disable_rule`/
   `set_config`/`register_scanner` → `ChangeSet`; one persister applies it and
   appends to the per-scope journal (`config/history.*`). Undo/revert build
   inverse ChangeSets. Fully unit-tested with spoofed adapters (no files, no
   terminal).
3. **CLI surface (AI-facing).** `keur rules enable/disable [--until/--for/--reason]`,
   `keur config list/get/set/history/undo/revert/stash`,
   `keur scanner register/unregister/list`, all with `--scope` and `--json`.
   This is the headless management API — an AI can run keur end-to-end, and undo
   its own mistakes, after this step, before any TUI exists.
4. **Platform input.** `enter_raw_mode()` + `read_key()` on posix & win32, smoke
   test per OS (CI builds all three).
5. **Pure model + reducers + view.** `manager/model.*`, `manager/view.*`,
   unit-tested with spoofed state; reducers emit `ChangeSet`s (same type step 2
   consumes). No terminal needed for these tests.
6. **`cmd/manager.cpp` loop** tying it together; one line in `main.cpp` dispatch
   + a `usage()` entry. The loop delegates every write to the step-2 ops layer.
7. **`manager --status [--json]`** non-interactive path (prints the tree; JSON
   for machines) — parity closed.

## Iterative build plan (layer by layer)

Each layer is independently useful, ships with unit + e2e tests (author-side),
and has one concrete thing to user-test. No layer depends on the next. Per-layer
loop: author writes code + tests, runs `cd src/keur && make test` green + the
self-check clean, hands the user a command to try, user tests, we iterate on
*that* layer before moving on. No commits — we just run the checks.

**Layer 0 — TOML writer + scope stack (foundation).**
Add `read_file`/`write_file` to the `Fs` adapter (non-executable, via
`platform::`). `config/scopes.*`: generalise `resolve_config` into the full
layer stack (keep the existing one working — `check`/`version` use it).
- Author tests: unit round-trip (write key → read back, comments/order
  preserved); stack resolution with a spoofed `Fs`/env.
- User test: `keur config list` shows your real layers (global + repo).
  Read-only, safe.

**Layer 1 — disable list in the engine + `rules enable/disable`.**
Parse `[rules] disabled`; loader skips those ids. CLI: `keur rules disable <id>`
/ `enable`.
- Author tests: unit (rule id in disabled set is skipped); e2e (`keur scan` a
  fixture, disable the rule, finding disappears).
- User test: disable a real rule in this repo, stage something that would
  trigger it → gate passes; enable → blocks again.

**Layer 2 — change lifecycle (journal + undo).**
`config/history.*` (journal ChangeSets), `keur config history` / `undo`.
- Author tests: unit (apply → inverse → back to original; journal append).
- User test: disable, `keur config undo`, rule is back.

**Layer 3 — headless parity complete.**
`keur config get/set`, `keur scanner register/list`, `--json` everywhere,
`keur manager --status --json`.
- Author tests: e2e (validate JSON output; `--scope` writes the right file).
- User test: let an AI drive keur purely via flags.

**Layer 4 — platform input.**
`enter_raw_mode()` + `read_key()` in `platform_posix.cpp` / `platform_win32.cpp`.
- Author tests: e2e smoke per OS (CI builds all three).
- User test: a tiny throwaway keydump — arrows/hjkl map to the right enum.

**Layer 5 — pure model + view + reducers.**
`manager/model.*`, `manager/view.*`. No terminal.
- Author tests: unit (`move`/`toggle`/`expand` reducers; view snapshot).
- User test: n/a (no UI yet) — author dumps a rendered frame to stdout to review.

**Layer 6 — `cmd/manager.cpp` loop.**
Wire it together; one line in `main.cpp`.
- Author tests: e2e (start, feed keys via a pipe, expect exit).
- User test: run **`keur manager`** for real — navigate, toggle, save, `q`.

## Open questions for you

1. **Command name:** `keur manager` as you said, or fold it into
   `keur configure` (interactive tree = the interactive mode of configure)?
   Keeping `configure` for guided setup and `manager` for the tree is clean, but
   two commands to explain.
2. **Disable scope default:** repo `keur.toml` or global? (I lean repo, with a
   modifier key for global.)
3. **Rules only, or also composite/facts nodes?** Composite rules have children
   (sub-patterns); the tree could show that hierarchy too, later.
4. **`Q` = quit-without-save**: want that power-user escape hatch, or keep it to
   just `q` + prompt?
5. **Journal retention:** keep `changes.jsonl` unbounded (like the learn loop's
   history), or cap/rotate it? And do you want the repo-scope journal committed
   (audit trail in git) or gitignored (local only)?
6. **Temp-disable expiry action:** when a `disabled_until` lapses, silently
   re-enable, or re-enable *and* surface a one-time notice on the next run?
7. **Stash vs git:** for repo-scoped config that's already in git, is keur's own
   `stash` redundant (just use `git stash`/branches), or do you want one uniform
   keur-native stash that works the same for global (non-git) and repo scopes?
```
