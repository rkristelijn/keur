# Running keur in an isolated sandbox (for reproducible demos)

Demos must look the same on every machine. keur normally layers rules and
config from the host, which is exactly what you want day-to-day but makes a
recording depend on *your* machine. `sandbox.sh` simulates isolation so a demo
is hermetic — no code change to keur needed.

## Why keur is already isolatable (no C++ change)

Verified against the source (src/keur/util/keur_utils.h `rule_dirs()`,
cmd/policy.cpp, config/scopes.cpp):

- Every path-deciding input comes from the environment or the binary location:
  `KEUR_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `HOME`, `KEUR_RULES_DIRS`,
  `PWD`. No hidden config source, no hardcoded machine paths in runtime code.
- Unset `HOME`/`XDG` degrades gracefully (the user rule/config layers are
  skipped, no crash).
- `keur check` / `keur scan` are offline and write nothing outside stdout/stderr
  (network + cache writes live only in `learn`/`train`, opt-in).

So a clean environment is enough. `sandbox.sh` provides it.

## The rule layers, and what the sandbox silences

`rule_dirs()` loads, in order:

1. `$KEUR_HOME/rules` — shipped repo rules. **Sandbox keeps this** (points
   KEUR_HOME at the repo).
2. `$XDG_CONFIG_HOME/keur/rules` (or `~/.config/keur/rules`) — global/company.
   **Sandbox silences it** (empty temp HOME/XDG).
3. `~/.keur/rules` — personal PRIVATE vault (PII/BII denylist, e.g.
   `VAULT-GENERIC`). **Sandbox silences it** (empty temp HOME). This is the one
   that made the PII demo non-reproducible: it fired a `block` from Remi's
   machine that no colleague has.
4. `<git-root>/.keur/rules` — project layer, resolved from the **CWD**, not an
   env var. **Sandbox controls it by running from a scratch dir** outside any
   repo with `.keur/rules`.
5. `$KEUR_RULES_DIRS` — explicit. Sandbox leaves it UNSET unless a demo passes a
   self-contained ruleset.

Global config (`~/.config/keur/keur.toml`, which can disable rules) is silenced
the same way (empty XDG/HOME), so the sandbox shows keur's *default* policy, not
the host's tuned one.

## Usage

```sh
docs/demo/sandbox.sh keur scan <path>     # any keur command, hermetically
docs/demo/sandbox.sh keur check
KEUR_BIN=/path/to/keur docs/demo/sandbox.sh keur version   # pin the binary
```

Mechanism: `env -i` wipes the environment, then re-adds only a controlled set
(clean temp HOME/XDG, KEUR_HOME at the repo, a minimal PATH). See the script.

## Demo tips (lessons learned — apply to every new demo)

1. **Always run a demo through the sandbox**, never against your machine config.
   Otherwise your personal vault / global config leaks into the recording and it
   is not reproducible. Verify with: does the output differ between
   `sandbox.sh keur scan X` and a plain `keur scan X`? If yes, the plain run is
   contaminated.

2. **CWD is an isolation input, not just env.** The project rule layer
   (`.keur/rules`) is found by walking up from the current directory. Run demos
   from a throwaway dir (e.g. `/tmp/demo`), never from inside a repo that has its
   own `.keur/rules`.

3. **Prefer `keur check` on a staged file over `keur scan .`** when the demo is
   about one finding. `scan .` on a bare repo also emits repo-hygiene findings
   (missing README/LICENSE/SECURITY/config: KEUR-DOC-*, PROJ-*, KEUR-CFG-*),
   which bury the finding you want to show. `check` looks at staged files, so the
   demo stays focused. If you must `scan`, either scan the single file
   (`keur scan config.py`) or give the demo repo a minimal README/LICENSE/config.

4. **Pin, don't fetch.** For an install demo, pin the release version or use a
   local asset in a hidden Arrange step — never fetch "latest" live, or the tape
   rots when a new release ships.

5. **Self-contained rulesets.** If a demo needs a custom rule (e.g. the
   client-repo demo), ship the `.rule` inside the demo dir and point
   `KEUR_RULES_DIRS` at it via the sandbox, so the tape carries its own rule and
   works anywhere.

6. **Verify before you render.** Use `docs/demo/run.sh <tape>` (which runs the
   tape's typed commands) to confirm the Assert still matches real output before
   spending time on a GIF. A demo whose Assert no longer matches is worse than no
   demo.

7. **Dogfood: run keur on your own demo files before recording.** The demo
   artefacts (docs, rules, scripts) must pass the gate they demonstrate. This is
   not theoretical: `SANDBOX.md` / `DEMO-PLAN.md` were first created in
   ALL-CAPS and keur's own `KEUR-NAME-010` flagged them (ALL-CAPS .md is reserved
   for root docs; everything else is lowercase/kebab) — renamed to `sandbox.md` /
   `demo-plan.md`. Check with: `docs/demo/sandbox.sh keur scan docs/demo/`.
