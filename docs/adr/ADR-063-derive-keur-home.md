# ADR-063 — Derive keur's home from the binary, not a required $KEUR_HOME

@id ADR-063

## Status

Accepted · 2026-10-25 · Coverage: code
· Relates to: ADR-005 (platform abstraction — the OS-specific exe-path lookup
lives in `platform::`), ADR-002 (single binary — the binary knows where it is),
ADR-051 (rules distribution & install-sync — where `rules/` lives relative to
the binary), ADR-035 (self-explaining — a silent gate is the opposite of this)

## Context

keur resolved its home — the install root holding `rules/` and `config/` —
*only* from the `$KEUR_HOME` environment variable. Four call sites read
`getenv("KEUR_HOME")` directly (`util/keur_utils.h` `rule_dirs()`,
`cmd/policy.cpp` config resolution, `cmd/learn.cpp` + `cmd/train.cpp`
`keur_home()`, `config/scopes.cpp`), and the standalone engine
(`src/rules/main.cpp`) had its own copy.

The failure this caused is severe and silent: **a git hook runs in a minimal
environment that does not export `$KEUR_HOME`.** With it unset, `rule_dirs()`
contributed no built-in layer, so the hook loaded **zero built-in rules** and
every `keur check` returned PASS — the gate was off, with no error. Proven: the
same commit carrying a hardcoded secret BLOCKs when `KEUR_HOME` is set and
PASSes when it is not. For a security gate, a silent no-op is the worst
possible default (contra ADR-035: a human or agent should always know the
state).

A binary already knows where it lives. git, cargo, and friends locate their
support files relative to the executable, not via a required env var. keur
should too.

## Decision

Resolve keur's home in this order (first hit wins), everywhere:

1. **`$KEUR_HOME`** if set and non-empty — an explicit **override**, unchanged.
2. **Derived from the binary**: take `executable_path()`, go up from
   `<home>/bin/<exe>` to `<home>`, and accept it only if it looks like a real
   install — `<home>/rules/` exists or `<home>/defaults/keur.toml` exists. The
   marker check stops a stray `bin/` on `$PATH` from being mistaken for a keur
   install.
3. Otherwise **empty** — keur falls back to the global and project rule layers
   (`~/.config/keur/rules`, `.keur/rules`) exactly as before.

The OS-specific executable lookup is a new `platform::executable_path()`
(ADR-005): `/proc/self/exe` on Linux, `_NSGetExecutablePath` + `realpath` on
macOS, `GetModuleFileNameW` on Windows. The shared derivation lives in
`keur::derive_keur_home()` (`util/keur_utils.h`); every app call site uses it,
and the standalone engine runs the same logic through the platform seam it
already links.

## Consequences

- **Positive**: the hook gates correctly out of the box — install keur to
  `<prefix>/bin/keur` with `<prefix>/rules/` beside it and the built-in rules
  load with no environment setup. The silent-gate regression is closed.
- **Positive**: `$KEUR_HOME` becomes an optional override (CI, odd layouts, a
  non-adjacent rules dir), not a setup precondition. Removes the env-fragility
  that made tests and fresh machines brittle.
- **Neutral**: when keur is run from a build tree (no `bin/` + `rules/` install
  layout, e.g. the test harness), derivation yields empty and behaviour is
  identical to before — tests that `unsetenv("KEUR_HOME")` still pass.
- **Cost**: one new platform function with three OS implementations, plus a
  marker check. Bounded and covered by a `@proves` test.
