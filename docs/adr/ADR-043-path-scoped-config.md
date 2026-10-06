# ADR-043 — Path-scoped config: per repo-root-class rules (work `lab/*` vs personal `hub/*`)

@id ADR-043

## Status

Proposed · 2026-10-06 · Coverage: none
· Relates to: ADR-003 (adapters + the pure config-layer resolver this extends —
`config/scopes.h` `resolve_layers`), ADR-042 (PII vault scanner — the rule class
that motivated this), ADR-035 (self-explaining findings + suppression — the
per-repo escape this replaces for the whole-class case), ADR-026/038/039
(difficulty cascade — the same global→repo fold this inserts a layer into),
`src/keur/config/scopes.h` (the `Scope` enum + `resolve_layers` this changes)

## Context

The config stack today resolves in five layers, general → specific, later wins
(`config/scopes.h`):

```text
Defaults → Global ($XDG_CONFIG_HOME/keur/keur.toml) → KeurHome ($KEUR_HOME/config)
         → Repo (<git-root>/keur.toml) → Explicit (--config)
```

That gives two useful granularities: **global** (all my repos) and **per-repo**
(one repo's `keur.toml` / `.keurignore`). It has **no** granularity in between —
"all repos under this directory".

The gap surfaced concretely (2026-10-06). A PII-vault rule (ADR-042, from a
work-oriented pattern list) fired on a **work** repo under `~/work/` because
the word "Vault" (the HashiCorp secrets backend, legitimate Terraform
vocabulary) matched. The rule is a **personal-vault** guard — it belongs on
personal projects, not on work IaC.

The machine has a clean, stable split the config cannot express:

| Root | Meaning |
|------|---------|
| `~/work/*` | **work** repositories |
| `~/personal/*` | **personal** projects |

With only global + per-repo, the two ways to fix the false positive are both
wrong:

1. **Disable the rule globally** → loses the personal-vault guard on
   every `hub/*` repo, where it is wanted. Over-broad.
2. **Suppress per work-repo** (inline `keur:ignore`, or `.keurignore` /
   `keur.toml` in each `lab/*` repo) → N copies that drift, re-done for every
   new work repo. Per-repo toil — the opposite of "build once, scale" and the
   exact symptom-patching this project exists to avoid.

What is missing is a layer that says *"for everything under `~/work`, apply
this config"* — once, centrally, for a whole class of repos.

## Decision

Add a **path-scoped config layer** between `Global` and `KeurHome` in the
resolver. A new global file maps directory-prefix globs to config fragments; at
resolution time keur matches the current repo root against those prefixes and
folds the matching fragment(s) in as an ordinary layer, so **every existing
mechanism** (disabled rules, difficulty vector, scanners) works unchanged — this
adds a *scope*, not a new config language.

### New layer in the stack

```text
Defaults → Global → PathScoped → KeurHome → Repo → Explicit
```

`PathScoped` sits after `Global` (so it refines a global baseline) and before
`KeurHome`/`Repo` (so a specific repo can still override it). Add
`Scope::PathScoped` to the enum in `config/scopes.h`; the resolver stays a pure
function of a `ScopeEnv` snapshot (ADR-003) — only `repo_root` is matched, no new
I/O beyond reading the one scopes file (injected like the others).

### The scopes file

`$XDG_CONFIG_HOME/keur/scopes.toml` (else `~/.config/keur/scopes.toml`), outside
every repo. Each section is a directory-prefix glob; its body is a normal
`keur.toml` fragment (`[rules]`, `[difficulty]`, `[scanners.*]`):

```toml
# Work repos: the personal-vault PII rule does not apply here.
["~/work/**"]
[rules]
disabled = ["PERSONAL-VAULT-RULE-ID"]

# Personal repos: keep everything on (this block is optional — default is
# "inherit global"); shown for symmetry / discoverability.
["~/personal/**"]
```

Resolution: tilde-expand, then match each section's glob against the repo root
(longest / most-specific match wins; multiple matches fold general → specific,
same ordering rule as the layer stack). A repo under no section simply gets the
global baseline — fully backward compatible.

### Why a prefix map, not more enum layers

Hard-coding "work" / "personal" scopes would bake one user's layout into the
tool. A prefix→fragment map is generic: any user expresses any split
(`~/work/**`, `~/clients/acme/**`, a monorepo subtree) without a code change.
The matching is a pure string/glob op over `ScopeEnv.repo_root`, so it is
unit-testable with a spoofed snapshot exactly like `resolve_layers` today.

### Surfacing it

`keur config list` and `manager --status` already print the layer stack
(`scopes.h` exists for this). `PathScoped` becomes a visible layer with its
source path and the matched glob, so "why is this rule off here?" is answerable
from `keur config list` / `keur difficulty --explain` without guessing.

## Consequences

- **Positive**: the `lab/*` vs `hub/*` split (and any future split) is expressed
  **once**, centrally. New work repos inherit "no personal-vault rule"
  automatically — no per-repo suppression, no drift. Build once, scale.
- **Positive**: reuses the whole config vocabulary (disabled rules, difficulty,
  scanners) — a scope, not a new DSL. Minimal surface.
- **Positive**: fixes the ADR-042 class of false positive at the right level: a
  personal-data rule is simply not active on work roots, instead of being
  globally weakened or locally patched.
- **Negative / risk**: a third "where did this come from?" source. Mitigated by
  making `PathScoped` a first-class, labelled layer in `config list` /
  `--explain` (its path + the matched glob shown), not a hidden override.
- **Negative**: glob matching on paths is a portability seam (separators,
  tilde, symlinked roots). Keep it pure over the `ScopeEnv` string and
  normalise once (reuse the platform path helpers; no new I/O).
- **Cost**: `Scope::PathScoped` + a `scopes.toml` parser (reuse the TOML
  fragment reader) + prefix-match fold in `resolve_layers` + `config list`
  rendering + tests. No new config language, no per-rule changes.
- **Scope guard**: this does NOT move secret suppression. Secrets still suppress
  by value only (ADR-035 / keurignore.h) — a path scope must never silence a
  secret finding. The scopes file governs rule activation / difficulty, not
  secret-value suppression.

## Follow-ups

1. Add `Scope::PathScoped` to `config/scopes.h` and insert it after `Global` in
   `resolve_layers` (pure; snapshot-tested).
2. `scopes.toml` loader: tilde-expand + glob-match repo root → ordered matching
   fragments (reuse the TOML fragment reader; Fs seam for the one read).
3. Fold matched fragments into the existing `[rules] disabled` / difficulty /
   scanner resolution — no new merge logic beyond an extra layer input.
4. Render `PathScoped` in `keur config list` and `keur difficulty --explain`
   (source path + matched glob).
5. Docs: a short "work vs personal repos" example in the README Configuration
   section using `~/work/**` and `~/personal/**`.
6. Related UX (separate, ADR-035): the suppression hint printed under every
   finding (`cmd/check.cpp` `format_finding_line`) names only the inline
   `keur:ignore` route; extend it to also point at `.keurignore` and, where a
   whole class is meant, `scopes.toml`. Tracked here as the motivating UX but
   implementable independently.
