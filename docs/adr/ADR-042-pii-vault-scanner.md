# ADR-042 — PII vault scanner: a self-explaining, opt-in BII/PII gate reading `$PII_VAULT`

@id ADR-042

## Status

Proposed · 2026-10-05 · Coverage: none
· Relates to: ADR-019 (external scanners + dispatch — this is a new evidence
source), ADR-009 (rules are data — the vault is data, not code), ADR-002 (single
binary — read a file, do not shell out to a sister-repo script), ADR-035
(self-explaining findings + suppression), ADR-003 (adapters — vault read via the
Fs seam), `src/keur/config/scanners.h` (`install_hint` — the "how to set it up"
UX pattern this reuses)

## Context

Working cross-repo you must never commit **business-identifiable information**
(BII) or PII — a BSN, IBAN, licence plate, internal hostname, your username or
machine name — into a rule, a fixture, or any file. A central vault already
exists for exactly this:

- `~/.local/share/pii/patterns.d/*.pii` (overridable via `$PII_VAULT`), five
  lists (`work` ~593 lines, `personal`, `gdpr-nl`, `gdpr-uk`, `generic`),
  `700`/`600` perms, documented against NIST SP 800-122 / ISO 27701 / ISO 27001
  A.8.12. Format: one regex per line, `#` = comment, blank skipped.
- It is already consumed by **cpm's** check-pii and by a global git hook
  (`~/.config/git/hooks/lib/no-pii.sh`) via `$PII_VAULT`.

**The problem (found 2026-10-05):** keur's git hooks now do `exec keur check`
only — they no longer source `no-pii.sh`, which lies dead in `lib/`. So in every
repo that uses this hooksPath, **the PII gate silently disappeared**. Nothing
blocks a BSN/plate/internal-hostname from being committed today. A manual proof
(converting the 588 vault patterns to a temp `~/.keur/rules` rule and scanning
keur itself → 0 matches) confirmed both that keur is currently clean *and* that
the engine matches the vault patterns fine — so the fix is wiring, not capability.

We do NOT want the patterns copied into keur's `rules/` (that would leak them
into a public repo) nor a second denylist that drifts from the vault. keur must
read the SAME source the other consumers use.

## Decision

Add a **first-class, opt-in PII-vault scanner** to keur: when enabled, keur reads
`${PII_VAULT:-~/.local/share/pii}/patterns.d/*.pii`, treats each pattern as a
blocking PII finding, and runs it in the dispatch pipeline (ADR-019) like any
other evidence source. It is **off by default** and **self-explaining when
turned on** — the key UX the user asked for.

### Config (opt-in, like `presidio`)

```toml
[scanners.pii-vault]
enabled = false              # opt-in: off until the user turns it on
group = "pii"
default-action = "block"
# vault path resolves from $PII_VAULT, else ~/.local/share/pii
```

### Self-explaining enable (the asked-for UX)

When `enabled = true` **and** the vault is missing/empty, keur does not fail and
does not go silent — it prints a one-time, `install_hint`-style explanation
telling the user exactly where the vault goes, mirroring how a missing proven
tool prints its install hint today:

```text
keur: PII-vault scanner is ON but no vault found.
      Create the vault directory and add one or more pattern files:

        mkdir -p ~/.local/share/pii/patterns.d
        chmod 700 ~/.local/share/pii      # dir
        $EDITOR ~/.local/share/pii/patterns.d/work.pii

      Format: one regex per line; '#' starts a comment; blank lines ignored.
      Override the location with  export PII_VAULT=/path/to/pii
      The vault lives OUTSIDE every repo — git never sees it. (ADR-042)
```

So "turn it on → get told where the file/folder goes" is built into the enable
path, not buried in docs.

### Behaviour

- Read `$PII_VAULT/patterns.d/*.pii` via the Fs adapter (ADR-003); each
  non-comment, non-blank line is one regex → one blocking PII pattern
  (category `pii`, action `block`, source = the `.pii` filename, no value echoed).
- Scope = the staged diff, same as every other dispatch source — the gate flags
  only what this change introduces, not pre-existing lines.
- Honour the existing ecosystem UX so it is drop-in for anyone already on the
  vault: a `keur:ignore pii` line-suppress and the `.config/.pii-config`
  `disable <name>` list (the same knobs `no-pii.sh` documents).
- Findings never echo the matched value — only `source:file:line` — so the gate
  output itself cannot leak PII (same discipline as `scanners/pii.sh` had).
- Missing/empty vault with `enabled = false` → nothing happens (today's state).
  Missing vault with `enabled = true` → the explanation above, non-blocking.

### Why a scanner, not a `.rule`

A `.rule` would have to live somewhere; in `rules/` it leaks patterns into the
public repo, in `~/.keur/rules` it is a hand-maintained copy that drifts from the
vault. Reading `$PII_VAULT` directly means **one source, three consumers**
(cpm, the git hook, keur) with no copy and nothing in any repo — the whole point.

## Consequences

- **Positive**: restores the PII/BII gate that silently vanished, via the tool
  the hooks already call (`keur check`). No second hook, no copy, no drift.
- **Positive**: opt-in + self-explaining — a new machine turns it on and is told
  exactly how to set up the vault; a machine without a vault is never blocked by
  its absence.
- **Positive**: reuses the vault's existing governance (perms, ISO references)
  and suppression UX (`keur:ignore pii`, `.config/.pii-config`).
- **Negative / cost**: a new dispatch source + a `.pii` loader (trivial format),
  the enable-time explainer + tests. Must read the vault read-only and never
  copy it into a scanned/tracked location (a vault under a scanned dir would be
  self-defeating — guard the loader to the resolved `$PII_VAULT` only).
- **Trust**: the vault is user-owned (`700`/`600`), out of every repo; keur only
  reads it. No execution, unlike the retired `scanners/pii.sh` shell brug.

## Follow-ups

1. `[scanners.pii-vault]` config section (opt-in, default off).
2. `.pii` loader: `$PII_VAULT/patterns.d/*.pii` → blocking PII patterns (Fs seam,
   value never echoed). Unit-test the parse (comments/blanks/regex).
3. Enable-time explainer (reuse the `install_hint` pattern) when ON + no vault.
4. Wire `keur:ignore pii` + `.config/.pii-config disable` suppression.
5. Dispatch integration + e2e: a staged file with a planted (fake) pattern
   BLOCKS; absent vault + ON = explanation, no block; OFF = silent.
6. Delete the dead `scanners/pii.sh` / `rules.sh` and the unused `no-pii.sh`
   reference once this lands (tracked as a cleanup TODO).
