# ADR-069 — Pre-publish dry-run gates per distribution channel (npm / brew / apt)

@id ADR-069

## Status

Proposed · 2026-11-04 · Coverage: none
· Relates to: ADR-045 (go-public distribution sequence brew→npx→apt — these gates
guard the channels that ADR defines), ADR-062 (CI Action/component — the other
consumer of the release artifacts; same "prove the wrapper in CI" discipline),
ADR-002 (single prebuilt binary — what every channel ships), ADR-041 (tiered CI
— these are cheap PR-time checks), ADR-035 (self-explaining — a failing gate must
say which channel's publish shape is wrong), `install.sh` + the Release workflow
(the checksum contract the channels reuse)

## Context

keur ships three package-manager channels (ADR-045): a Homebrew formula
(`Formula/keur.rb` via `scripts/formula.sh`), an npm wrapper (`packaging/npm/`),
and a `.deb` (`scripts/build-deb.sh`). The `dist-smoke` CI job already proves the
channels **build and run** — it builds the `.deb`, installs it, runs the npm
launcher's lazy fetch, and syntax-checks the formula.

What `dist-smoke` does *not* prove is whether the **publish payload** is correct:
what would actually be uploaded to npm, whether the formula passes Homebrew's
own audit, and whether the `.deb` satisfies Debian packaging policy. Those are a
different question — "will the publish be accepted and correct?" versus "does the
wrapper work?" — and each channel has well-known ways to get it wrong:

- **npm**: publishing the wrong file set (too broad → leaks the repo; too narrow
  → ships a launcher that can't find its module), a name that is taken, or a
  version that already exists (npm publishes are **irreversible** — a version
  cannot be re-published).
- **Homebrew**: the wrong tap convention (the `homebrew-keur`-vs-repo-tap +
  untrusted-tap trap that bit us, fixed in the brew-doc PR), a `sha256` that
  does not match the described asset, or `brew audit` style/structure failures.
- **apt/.deb**: a malformed `DEBIAN/control`, wrong `Architecture`, root-owned
  files, or Debian-policy violations a bare install test does not surface.

The most dangerous of these (npm's irreversible publish) is exactly the one a
plain merge or a release-workflow `dry_run` does **not** exercise — the release
workflow's `publish` job is skipped on `dry_run`. So a broken publish shape would
first be discovered during the real, irreversible release.

## Decision

Add **per-channel, per-concern dry-run gates** as **separate CI jobs** — not
extra steps bolted onto `dist-smoke`. Each job answers one question for one
channel and fails independently:

- **`npm-dryrun`** — `npm publish --dry-run` + inspect `npm pack` contents.
  Proves the exact upload payload (file set, no binary, no postinstall) without
  publishing.
- **`brew-audit`** — `brew audit` / `brew style` on the generated formula.
  Proves Homebrew would accept the formula's structure and style.
- **`deb-lint`** — `lintian` on the built `.deb`. Proves Debian packaging policy
  (control fields, permissions, layout) beyond "it installs".

`dist-smoke` stays as-is ("does the wrapper build + run"); these three stay
separate ("is the publish shape correct").

### Why separate jobs, not steps in `dist-smoke`

- **Diagnosis**: a red `npm-dryrun` says "publish payload wrong" immediately,
  independent of whether the build is fine — no log-digging through a mega-job.
- **Granular required-checks**: when branch protection / a merge queue is set up,
  each job can be chosen blocking or advisory individually. The build/smoke can
  be blocking while the (sometimes noisy) linters start advisory.
- **Parallelism**: separate jobs run on separate runners, concurrently.
- **Reuse**: a standalone publish dry-run can also be called as a pre-flight in
  the Release workflow, not only on PRs.
- **Consistency**: keur already separates CI concerns into their own jobs
  (`dogfood`, `action-dogfood`, `dist-smoke`); these fit that shape.

### Blocking policy: start advisory

`lintian` and `brew audit` are opinionated and emit warnings about things that do
not actually break an install. To avoid blocking PRs on cosmetic policy opinions,
these gates **start non-blocking** (report, do not fail the job) and are promoted
to blocking only once their output is clean. `npm-dryrun` can be strict from the
start — its failure modes (bad file set, name/version) are real, not cosmetic.

## Consequences

- **Positive**: the irreversible npm publish, the formula audit, and Debian
  policy are all exercised **before merge**, on every PR — the real publish shape
  is proven without an irreversible action.
- **Positive**: a broken channel fails CI with a channel-specific, self-
  explaining signal (ADR-035) instead of surfacing during the real release.
- **Cost**: three extra CI jobs = more runner minutes and more workflow YAML. For
  a repo this size that is acceptable; the jobs are cheap and parallel.
- **Cost / named risk**: `lintian`/`brew audit` can be noisy; mitigated by
  starting them advisory and promoting to blocking only after the noise is
  triaged, so they never block a PR on a cosmetic opinion.
- **Neutral**: these gates validate *shape*, not the act of publishing — the real
  publish still happens only in the Release workflow (ADR-045), guarded by these
  dry-runs upstream.

## Follow-ups

1. Add the three jobs to `.github/workflows/ci.yml`, advisory for `brew-audit` +
   `deb-lint`, strict for `npm-dryrun`.
2. Once the advisory output is clean, flip `brew-audit` / `deb-lint` to blocking.
3. Optionally call `npm-dryrun` as a pre-flight step in the Release workflow's
   `publish` job, so the real release re-checks the payload immediately before
   the irreversible `npm publish`.
