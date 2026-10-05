# ADR-008 — Incremental compile (`-MMD -MP`) and a note on build-affected

@id ADR-008
**Status**: accepted (part 1) / proposed (part 2) · **Date**: 2026-09-18 · **Coverage**: none <!-- Makefile build behaviour, not a code site -->

## Part 1 — Automatic header dependency tracking (DONE)

### Context

`src/keur/Makefile` compiles per translation unit and previously listed each object's header
prerequisites **by hand**:

```make
$(BINDIR)/rule_scan.o: ../rules/rule/scan.cpp ../rules/rule/internal.h ../rules/rule_engine.h ...
```

Correct while maintained, but fragile: add a new `#include` and forget the Makefile line, and make
reuses a stale `.o` — a silent, confusing bug (we hit a variant of this: only `keur-rules` was
rebuilt, not `keur`, after a shared-header change).

### Decision

Add `-MMD -MP` to `CXXFLAGS` and `-include` the generated `.d` files. The compiler now emits the
real include graph per object; make rebuilds exactly what a changed header touches, automatically.
Kept the explicit prerequisites as belt-and-braces (harmless; the `.d` files are authoritative).

- `-MMD`: emit `.d` dependency files as a side effect of compiling (no extra pass).
- `-MP`: add phony targets for headers so a **deleted** header doesn't break the build.
- `-include $(DEPS)`: pull them in; `-` ignores them on the first clean build.
- `clean` removes `.d` too.

### Consequences

Header edits trigger the right rebuilds without manual upkeep. Verified: touching
`rules/rule/internal.h` rebuilds only the 4 rule objects + relink. Works identically on macOS and
Linux (both GCC/Clang support `-MMD -MP`).

## Part 2 — Build-affected (Nx-style), PROPOSED / TODO

### Idea

Go beyond file-level incremental compile to **target-level affected**: from a `git diff` against
the base branch, decide which *targets* (build `keur`, build `keur-rules`, unit, integration, e2e,
lint) actually need to run, and cache results. This is the Nx `affected`/`nx-cloud` model.

### Why not now

keur is a single binary from ~20 `.cpp` files — `-MMD -MP` already covers the compile side, and the
CI matrix (3 OS builds + dogfood) is small. Full affected-graph + caching is real machinery whose
payoff is marginal at this size.

### Where it IS worth it (the actual TODO)

The leverage is not keur itself but the **fleet**:

- `security-tooling` (many independent provider scripts) — run only the affected provider's tests.
- the 648-repo GitLab estate (gitlab-port-scanner) — "scan only what changed since last run" is the
  same idea one layer up, and directly reduces CI time.

**TODO**: evaluate a git-diff-driven affected runner for `security-tooling` / the scanners before
investing in one for keur. Track as a separate ticket; do not build it into keur's Makefile now.

### Options if/when we do keur itself

- Order-only prerequisites + timestamp caching (plain make) — cheap, no deps.
- `ccache` for compile caching — drop-in, helps clean rebuilds in CI.
- A tiny `make affected` target that diffs against `origin/main` and picks targets — bespoke but small.
