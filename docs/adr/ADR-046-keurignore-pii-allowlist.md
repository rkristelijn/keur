# ADR-046 — Allow a PII/BII false positive like a spellchecker dictionary: value entries in .keurignore

@id ADR-046

## Status

Proposed · 2026-10-06 · Coverage: none
· Relates to: ADR-035 (.keurignore — the single suppression file; this extends
its value-suppression from "secret" to "pii"), ADR-042 (PII-vault scanner — the
source of VAULT-* findings), ADR-043 (path-scoped config — work vs personal
repos), `config/keurignore.h` (the mechanism)

## Context

The PII-vault scanner (ADR-042) flags terms from the user's `~/.local/share/pii`
lists — e.g. the owner's GitHub handle `rkristelijn` (in `personal.pii`). That is
correct for a personal repo. But keur itself is published publicly as
`rkristelijn/keur`, so that handle appears legitimately in the installer URL,
LICENSE, and docs — it is authorship, not a leak. Today that blocks the commit
with no clean, scoped way to say "this term is fine HERE."

The right mental model is a spellchecker: `rkristelijn` is globally "misspelled"
(not-to-leak), but a project can add it to its OWN dictionary as "correct here."
That is add-a-word, NOT turn-the-spellchecker-off. There are already several
suppression paths (inline `keur:ignore`, `.keurignore` rule-id, keur.toml
`[rules] disabled`), and the sprawl is itself the problem: none is the obvious
"allow this one value in this repo" move, and turning the whole VAULT rule off is
too blunt (it would stop catching every OTHER personal term).

`.keurignore` (ADR-035) already does exactly the needed primitive for secrets:
suppress BY VALUE (`secret <VALUE|sha256:HASH>`), robust across moves, re-caught
if the value changes. PII is the same shape — a value you deliberately allow —
but `keurignore_suppresses` currently gates value-suppression to category
"secret"/"secrets" only, so PII cannot use it.

## Decision

Use the existing `.keurignore` as the per-repo dictionary, with an `allow <term>`
entry. The allow check runs IN THE SCAN LAYER, at the moment a pattern matches,
NOT afterwards on the finding: the engine holds the matched line there, so it can
weigh the blocklist (the pii/secret patterns) and the allowlist (the `allow`
terms) together and decide green/red per line — the spellchecker model (the
"misspelled" list and the custom dictionary are consulted together, on the word).

```
# .keurignore — this repo's allowlist of otherwise-flagged terms (like a
# spellchecker custom dictionary). An allowed term on a line cancels a pii/secret
# match on that same line; every OTHER term is still caught.
allow rkristelijn
```

Why in the scan layer, not on the finding (corrected design):
- A finding does not carry the matched substring (only a generic message), so a
  post-hoc value compare in `check.cpp` cannot work. The scan layer DOES have the
  raw line at match time (next to the existing inline `keur:ignore` suppression),
  so the allow check belongs there, alongside the blocklist.
- Mechanism: when a pii/secret pattern matches a line, the engine also checks the
  `allow` terms against that same line; if an allowed term is present, the match
  is dropped. Blocklist and allowlist evaluated together → green or red.

Semantics:
- `allow <term>` is a value entry; it cancels a `pii`/`secret`/`bii` finding on a
  line that contains that term. It never disables the rule — every other term on
  any line is still caught.
- Only pii/secret categories are allow-cancellable (a code finding still uses
  path/line/rule suppression). You cannot silence the whole sweep with one allow.

Why term-allow, not rule or line:
- Rule-id (`VAULT-PERSONAL`) would disable the entire personal-PII check — turn
  the spellchecker off. Wrong.
- Line (`install.sh:11`) is brittle (moves break it) and PII must suppress by
  value by design (ADR-035). Wrong.
- Term (`allow rkristelijn`) is exactly add-a-word: scoped to this repo, keeps
  every other term caught, survives line moves, re-fires if a DIFFERENT term
  appears. Right.

## Why not the alternatives

- **A new `.config/pii-allow` file**: a second suppression surface to learn and
  keep in sync. `.keurignore` is already "the one place" (ADR-035); reuse it.
- **keur.toml `[rules] allow`**: PII is a scanner value, not a rule toggle;
  value-allow does not belong in a rule-id list. And it would duplicate the
  `.keurignore` value primitive.
- **Inline `keur:ignore` in install.sh**: pollutes a public file users read, and
  repeats per occurrence; a value entry covers all occurrences from one place.

## Consequences

- **Positive**: one obvious, scoped way to allow a legitimately-present term;
  the rule stays on for everything else. The spellchecker model, exactly.
- **Positive**: no new file/format — a one-word alias + one category in an
  existing pure function. Small.
- **Negative / caution**: `allow` suppresses that value in ANY file in the repo
  (by design — authorship appears in many files). It does NOT weaken detection
  of other values. An `allow` of a real secret would be misuse; `allow` is for
  values that are deliberately public (handles, org names), not credentials.
- **Scope**: the committed `.keurignore` is per-repo, so allowing `rkristelijn`
  in keur does not allow it in a work repo (ADR-043 spirit).

## Follow-ups

1. `config/keurignore.h/.cpp`: parse `allow <value>` as a value entry; extend
   `keurignore_suppresses` to cover category `pii` for value entries. Pure;
   unit-test with a spoofed finding.
2. Ensure the PII-vault scanner (ADR-042) consults `.keurignore` on each finding.
3. keur's own `.keurignore`: `allow rkristelijn` (with the authorship comment),
   unblocking the public install.sh.
4. Document the one-liner in CONTRIBUTING/README: "allow a legitimately-present
   term like a spellchecker dictionary — `allow <value>` in .keurignore."
