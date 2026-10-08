# ADR-051 — Rules distribution: install/sync the bundled ruleset, not just the binary

@id ADR-051
**Status**: proposed · **Date**: 2026-10-08 · **Coverage**: none (install path + a `keur rules --sync`/`make install-rules` to be added) · **Relates to**: ADR-001 (native rule engine — the rules are its data), ADR-002 (single C++ binary — the binary is self-contained, the ruleset is not baked in), ADR-011 (project profile / maturity — the installed set is also difficulty-filtered at scan time), ADR-028 (cpm decommission — the `~/.config/keur/rules` snapshot was imported from cpm), ADR-043 (path-scoped config / rule-dir resolution)

## Context

keur ships two things that must stay in step: the **binary** (`bin/keur`) and
the **ruleset** (`rules/**/*.rule`, data for the engine — ADR-001/009). The
binary is self-contained; the ruleset is **not baked in** (ADR-002 keeps rules
as external data so a rule change is not a recompile). The install story only
covers the binary:

```bash
install -m 755 bin/keur /usr/local/bin/keur     # README "Build" step
```

Nothing installs or updates the rules. At scan time the engine resolves rule
directories (`util/keur_utils.h::rule_dirs`, ADR-043 order):

1. `$KEUR_HOME/rules`
2. `$XDG_CONFIG_HOME/keur/rules` (default `~/.config/keur/rules`)
3. `~/.keur/rules` (personal, cross-repo private rules)
4. project `.keur/rules` (walking up from CWD)
5. `$KEUR_RULES_DIRS` (explicit override, `:`-separated)

With `KEUR_HOME` unset, a plain `keur scan` uses whatever happens to be in
`~/.config/keur/rules` — a directory that today is a **hand-imported snapshot**
(originally the cpm security/secrets/supply-chain families, ADR-028). It has no
link to the repo's `rules/` tree and no update mechanism.

### The failure this ADR responds to (2026-10-08)

While improving the Express TypeScript rules (`EX-SEC-010` no-helmet,
`EX-SEC-011` body-parser limit) against the `mestjs` fixtures, the rules
"did not fire" under `keur scan`. Time was spent suspecting the rules and the
engine. Root cause was neither:

- The installed `~/.config/keur/rules/security/` held **58** rules; the repo's
  `rules/security/` held **365**. `EX-SEC-010/011` and `WEB-SEC-016` simply were
  not present in the installed snapshot.
- Proven by running the standalone engine against the repo tree:
  `KEUR_RULES_DIRS=$PWD/rules bin/keur-rules --files <fixture>` fired the rules
  immediately and correctly (TP on the insecure fixture, no FP on a secure one).

So the rule logic was fine. The distribution was stale. This is the **worst
class of bug — a silent false negative from configuration, not code**: the tool
reports "0 findings" with full confidence while the rule that would have fired
was never loaded. A second, compounding factor: even once present, a rule is
subject to the maturity/difficulty filter at scan time (ADR-011), so "present in
the dir" and "active for this scan" are two different things.

A documentation stop-gap already landed (CONTRIBUTING "Writing rules": test
against the repo `rules/` via `KEUR_RULES_DIRS` + `keur-rules`; rules-authoring
traps for `skip_comments`/`content_contains`). This ADR addresses the
structural cause behind that advice.

## Decision

Treat the bundled ruleset as a **versioned, installable artifact that travels
with the binary**, and make staleness **loud, not silent**.

@id ADR-051.install

1. **Install the rules alongside the binary.** Add a `make install` (and a
   matching README step) that installs *both*:
   - `bin/keur` → `/usr/local/bin/keur`
   - `rules/**` → `$KEUR_HOME/rules` (default `$KEUR_HOME` = the install prefix,
     e.g. `/usr/local/share/keur`), and set/record `KEUR_HOME` so resolution
     step 1 points at the installed-with-the-binary set.

   The binary's bundled rules become the **built-in layer** (ADR-043 step 1),
   exactly as the resolver already documents ("`$KEUR_HOME/rules` is the
   built-in set"). `~/.config/keur/rules` reverts to its intended role: a
   *global/company overlay*, not the primary source.

@id ADR-051.sync

2. **A first-class sync command.** Add `keur rules --sync` (and a thin
   `make install-rules`) that copies the repo/bundled `rules/` into the resolved
   built-in location, reporting a diff (`N added, M changed, K removed`) so an
   update is visible and auditable. Idempotent; no network.

@id ADR-051.staleness

3. **Make staleness detectable.** Bake a **ruleset fingerprint** (count + a hash
   of rule ids+versions) into the binary at build time, next to `KEUR_VERSION`
   (same mechanism as the version string, README "Build"). On `keur scan`/`check`
   startup, compare the fingerprint of the *loaded* rules against the baked-in
   expectation and, on mismatch, print a one-line WARNING to stderr:

   ```
   keur: loaded ruleset differs from the one shipped with this binary
         (loaded 58 rules from ~/.config/keur/rules; binary expects 365).
         Run `keur rules --sync` or set KEUR_HOME. See ADR-051.
   ```

   This converts the silent-FN into a visible, actionable notice. It is a WARN,
   never a BLOCK (an intentional overlay/override is legitimate — ADR-043).

4. **`keur doctor` reports the rule-source truth.** Extend `doctor` to print the
   resolved rule dirs *in order*, the rule count per dir, and which dir "won" for
   a sample extension — so "why didn't my rule fire" is answerable in one
   command instead of a C++ spelunk.

## Consequences

**Positive**
- The ruleset ships and updates with the binary; a fresh install is complete,
  not a binary pointed at an empty/stale rule dir.
- Silent false negatives from a stale snapshot become a loud, actionable WARN.
- `~/.config/keur/rules` is cleanly demoted to an overlay; the layering in
  ADR-043 is honoured in practice, not just on paper.
- `doctor` makes rule-source resolution observable (supports the CONTRIBUTING
  guidance instead of relying on it).

**Negative / risk**
- `make install` now has two payloads (binary + rules); packaging must keep them
  versioned together. Mitigated by the baked-in fingerprint — a mismatch is
  detected at runtime regardless of how install happened.
- A fingerprint check on every invocation must stay sub-second (ADR-002 hook
  budget). It is a count + a precomputed hash over already-loaded rule metadata,
  so it is O(rules) over data already in memory — negligible.
- Overlays legitimately change the loaded set, so the WARN must not cry wolf.
  Mitigation: fingerprint the **built-in layer only** (step 1 dir), not the
  union; an overlay in a later layer does not trip it.

**Rollout**
- Land `make install` (binary + rules + `KEUR_HOME`) and the README update
  first; this alone fixes fresh installs.
- Add `keur rules --sync` + `make install-rules` for the update path.
- Add the build-time fingerprint and the startup check last (it depends on the
  install location being settled), behind the existing version-baking mechanism.
- Re-sync this machine's `~/.config/keur/rules` (or set `KEUR_HOME` to the repo)
  so the environment that produced the 58-vs-365 gap matches the repo.

## Alternatives considered

- **Bake the rules into the binary (compile-time embed).** Rejected: it defeats
  ADR-002's reason for keeping rules as external data (a rule edit would be a
  recompile) and kills the `KEUR_RULES_DIRS` overlay story. The fingerprint
  gives the "shipped together" guarantee without the embed.
- **Do nothing; rely on the CONTRIBUTING note.** Rejected: documentation did not
  prevent the failure and will not prevent the next person (or agent) from
  scanning against a stale `~/.config/keur/rules`. The cause is structural.
- **Always require `KEUR_RULES_DIRS` explicitly.** Rejected as the default: fine
  for development (and now documented), but a shipped tool should work out of the
  box with a complete, self-consistent ruleset.

## Open questions

- Install prefix for the rules: `$KEUR_HOME/rules` under a fixed share dir
  (`/usr/local/share/keur`) vs. `~/.config/keur/rules` as the primary. Lean
  share-dir-as-built-in + `~/.config` as overlay, to match ADR-043 layering.
- Should `keur rules --sync` pull from the installed binary's bundled copy, or
  from a repo path when run inside the keur source tree? Probably both, with the
  source tree taking precedence when detected (dogfooding).
- Does the staleness WARN belong in `check` (the hook, sub-second budget) or only
  in `scan`/`doctor`? Lean: a cached fingerprint check is cheap enough for
  `check`, but gate it so it prints at most once per session.

## Same pattern elsewhere: generated-not-maintained indexes

The staleness this ADR fixes — a hand-kept copy drifting from its source — is a
recurring shape, not unique to the ruleset. The ADR index (`docs/adr/README.md`)
had the same risk: a manually curated list goes stale the moment someone adds an
ADR and forgets the index. It is now **generated** by `scripts/gen-adr-index.sh`
from the ADR files themselves, with a `--check` mode wired into the `dogfood` CI
job that fails if the committed index is out of date. The principle is identical:
derive from the source of truth, and make drift *loud* (a failing check / a
runtime WARN) rather than *silent*. Prefer this for any index or snapshot that
mirrors a set of files.
