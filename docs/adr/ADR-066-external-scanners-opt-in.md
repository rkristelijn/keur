# ADR-066 — External scanners are opt-in; the default gate is keur's own engine only

@id ADR-066

**Status**: Proposed · **Date**: 2026-10-25 · **Coverage**: none
· Relates to: ADR-002 (single self-contained binary — works with no external
deps), ADR-045 (go-public — first impression matters), the roadmap Milestone 2
("user chooses to install proven tools; keur uses them when present, degrades
gracefully when not"), ADR-035 (self-explaining — a missing-tool surprise is the
opposite), `config/keur.toml` (the tier scanner lists this changes)

## Context

The shipped `config/keur.toml` enabled external scanners **by default**: the
`pre-commit` tier listed `gitleaks`, and `pre-push`/`ci` listed trufflehog,
trivy, semgrep, checkov, grype, osv-scanner, tfsec. Two problems for a public
first release:

1. **It breaks the "single self-contained binary" promise (ADR-002).** A fresh
   `install.sh` + `keur hook --install` should gate out of the box with nothing
   else installed. Listing `gitleaks` by default means a user without gitleaks
   gets a skipped scanner (or a surprise) on their very first commit — a poor
   first impression, exactly at the moment we want keur to feel solid.
2. **It advertises capability that is not wired yet.** Today the check dispatch
   runs only the built-in RE2 engine (`keur-rules`); the external-tool libmode
   is not connected. A default list of eight tools the gate does not actually
   invoke is a promise the binary does not keep.

The user's call, and the right one: **do not encapsulate other tools by
default — let people turn them on themselves.**

## Decision

**The default gate, every tier, is `["keur-rules"]` — keur's own engine only.**
External scanners become strictly **opt-in**: a user adds the tool to a tier's
`scanners` list in their own `keur.toml` *and* installs the tool. keur uses a
listed tool when it is present on PATH and degrades gracefully (skips, never
blocks) when it is not — the Milestone-2 contract.

`config/keur.toml` ships the external tools as **commented, documented
examples** beside each tier, so turning one on is a one-line uncomment with the
rationale right there — discoverable, but off until chosen. The `[scanners.*]`
adapter definitions stay (they are inert until a tier references them).

This makes keur's identity honest: **the deterministic RE2 engine is the
product; proven external tools are an optional corroboration layer the user
opts into**, not a hidden dependency. keur is the orchestrator/referee; it does
not silently drag in a toolchain.

### What this is NOT

- **Not removing external-tool support.** The adapters, the corroboration model,
  and the tier mechanism stay. This flips the *default*, not the capability.
- **Not changing the engine.** `keur-rules` already runs in-process; this only
  changes which scanner names a fresh tier lists.

## Consequences

- **Positive**: a fresh install gates correctly with zero external dependencies
  (ADR-002 kept). No missing-tool surprise on the first commit.
- **Positive**: the config no longer advertises tools the dispatch does not yet
  invoke — the default matches reality.
- **Positive**: clear positioning for the public release — keur stands on its
  own engine; proven tools are a deliberate add-on (roadmap M2), not baggage.
- **Neutral**: a user who *wants* gitleaks-in-the-gate now uncomments one line
  (and installs gitleaks) instead of getting it implicitly. A fair trade for an
  honest, dependency-free default.
- **Cost**: none beyond the config edit + doc note; the libmode wiring (M2) is
  unchanged and still future work.
