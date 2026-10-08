# ADR-052 — Scan scope: file-level rules see the repo, per-line rules see the diff

@id ADR-052
**Status**: accepted · **Date**: 2026-10-08 · **Coverage**: code (`scan_impl` file-level scope; `rules_scan_files_scoped`) + tests (`@proves ADR-052.scope`) · **Relates to**: ADR-001 (native rule engine), ADR-002 (single binary — the hook must stay sub-second), ADR-014 (engine registry — file-level engines run outside the per-line registry), ADR-051 (rules distribution — the installed-vs-repo split surfaced this)

## Context

`keur check` (the git hook) scans the **staged diff** — the files `git` reports
as added/copied/modified. That is correct for per-line engines: a `pattern` rule
("this line concatenates SQL") only makes sense on the lines being committed.

But the engine also has **file-level** engines — `file-absence`, `file-presence`,
`max-dir-files`, `filename-case` — that ask a **repository** question, not a diff
question:

- `file-absence` ("the project has no SECURITY.md") — a property of the repo.
- `max-dir-files` ("this directory holds too many modules") — counts across the
  tree, not the diff.
- `filename-case` ("names must be kebab-case") — a convention over the repo.

The bug (observed 2026-10-08): committing any change to `mestjs` — a repo that
**has** a `SECURITY.md` — fired `KEUR-QUAL-002` ("Repository has no SECURITY.md")
on every commit, because `SECURITY.md` was not in *that commit's* staged set. The
file-level engine was handed only the staged files, concluded the file was
absent, and warned. A false positive on every unrelated commit — the kind of
noise that makes people reach for `--no-verify`.

The naive fixes are both wrong:
- **Scan the whole repo for everything** — breaks ADR-002's sub-second hook
  budget on a large repo, and makes per-line rules fire on unstaged code the
  author did not touch.
- **Drop file-level rules from the hook** — then "no SECURITY.md" is never
  caught at commit time, only in a full `scan`.

## Decision

Split the scan scope by engine class. @id ADR-052.scope

- **Per-line engines** (pattern/presence/absence/block-absence/count/multiline/…)
  run over the **staged diff** — unchanged, sub-second.
- **File-level engines** (file-absence/file-presence/max-dir-files/filename-case)
  run over the **whole repo tree**, because they answer a repo-wide question.

Implementation: `scan_impl` takes an optional second fileset,
`file_level_files`. When present, the file-level pass evaluates against it while
the per-line pass still uses the staged `files`; when absent, both use the same
fileset (so `rules_scan_dir` and the existing `rules_scan_files` are unchanged).
A new entrypoint `rules_scan_files_scoped(rules, staged_files, repo_root)` walks
`repo_root` once for the file-level fileset and passes the staged list for the
per-line pass. `keur check` calls it with the repo root from
`git rev-parse --show-toplevel`. If `repo_root` is empty it degrades to the old
`rules_scan_files` behaviour.

Cost: one directory walk per `check`, only when file-level rules are active.
The walk is the same one `rules_scan_dir` already does; it is bounded by the
repo size and runs once, not per rule. Per-line scanning — the hot path — is
untouched.

## Consequences

**Positive**
- `file-absence`/`max-dir-files`/`filename-case` answer the question they were
  written to answer; no more false positives on commits that simply don't touch
  the file in question.
- Per-line rules stay diff-scoped and sub-second; the hook budget (ADR-002) is
  preserved.
- Backward compatible: `rules_scan_files` and `rules_scan_dir` keep their exact
  behaviour; only the hook opts into the scoped variant.

**Negative / risk**
- One extra repo walk per `check` when file-level rules are active. Mitigated:
  it is bounded, cached by the OS, and skipped entirely if no file-level rule is
  loaded. If it ever shows on the hook budget, gate the walk behind "a
  file-level rule exists in the active set".
- A file-absence rule now reflects repo state, so staging a *deletion* of
  SECURITY.md will not fire it until the delete is on disk — acceptable, since
  the working tree is the repo truth at commit time.

## Testing

Two integration scenarios (`@proves ADR-052.scope`): a repo that HAS the file
but doesn't stage it → no finding; a repo that truly lacks it → still fires. So
the fix removes the false positive without silencing the real case.

## Note: this surfaced a second bug (rule-id de-duplication)

While verifying the fix, `KEUR-QUAL-002` appeared twice — not from this change,
but because the same rule id existed in two resolved rule dirs (the repo's
`rules/` and the installed `~/.config/keur/rules` snapshot, ADR-051). Rule
loading now de-duplicates by id (first dir in precedence order wins), so an
overlapping id is loaded and evaluated once. That is a loading fix, orthogonal to
scope; the broader question of combining *different* rules that report the *same*
underlying issue (e.g. `KEUR-QUAL-002` vs `PROJ-003`, both "no SECURITY.md") is
its own decision — see ADR-053.
