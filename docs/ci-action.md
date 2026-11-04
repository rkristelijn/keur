# HOWTO: run keur in your CI pipeline (GitHub Action / GitLab component)

Add keur as a CI stage with a few lines instead of a hand-written install
script. keur ships two thin wrappers over the same prebuilt binary + checksum
contract (ADR-062): a **GitHub composite Action** (`action.yml`) and a **GitLab
CI/CD component** (`templates/keur.yml`). Both just fetch the verified release
binary via `install.sh` and run `keur <args>` — no toolchain, no build.

> This is the CI counterpart to the local git hook (`keur hook --install`). The
> hook gates commits on your machine; the Action/component gates the same change
> in the pipeline (the `secret-scan`/`sast` stage of ADR-012).

## Context

A CI job runs keur against your code and fails the pipeline on a BLOCK verdict,
exactly like the pre-commit hook does locally. Because keur is a single static
binary with a clean env-contract (`KEUR_VERSION` pins the tag, `KEUR_BIN_DIR`
picks the dir — ADR-002), the wrapper is "fetch the binary, run `keur check`".
The binary is **checksum-verified before it is trusted** by `install.sh`
(SCA-030), so a tampered download is rejected.

## Prerequisites

- A CI runner with `curl` (and a POSIX shell) — the GitLab template installs
  these on its Alpine image automatically.
- Network access to this repo's public GitHub Release assets (the runner
  downloads `keur-<os>-<arch>` + `SHA256SUMS` anonymously at run time).

No compiler or language runtime is needed — the wrapper fetches a prebuilt
binary.

## GitHub Actions

Add a workflow that uses the Action. Minimal gate on every push and PR:

```yaml
# .github/workflows/keur.yml
name: keur
on: [push, pull_request]

jobs:
  keur:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: rkristelijn/keur@v1          # pin a tag in CI for reproducibility
```

With no inputs it runs `keur check` (the default). Exit 1 (BLOCK) fails the job.

### Inputs

The Action's inputs map onto the `install.sh` env-contract; the authoritative
list lives in [`action.yml`](../action.yml). Today:

| Input | Default | Purpose |
|-------|---------|---------|
| `version` | `latest` | keur release tag to install (→ `KEUR_VERSION`). **Pin a tag in CI.** |
| `args` | `check` | the keur subcommand + arguments to run |
| `working-directory` | `.` | directory to run keur in |

### Examples

Pin a version and run the stricter pre-push tier:

```yaml
      - uses: rkristelijn/keur@v1
        with:
          version: v0.2.0
          args: check --tier pre-push
```

Advisory, non-blocking scan of a subtree (scan never blocks — exit 0):

```yaml
      - uses: rkristelijn/keur@v1
        with:
          args: scan src/
```

Point at a subdirectory (monorepo package):

```yaml
      - uses: rkristelijn/keur@v1
        with:
          working-directory: packages/api
```

### Gating a PR's diff (optional)

`keur check` reads the staged diff (`git diff --cached`). On a PR you can gate
only the change by staging the diff against the base before the Action runs —
the same trick keur's own CI uses (`.github/workflows/ci.yml`, the `dogfood`
job). For most repos the plain `check` above is enough to start.

## GitLab CI/CD

Include the component and let it add a `keur` job:

```yaml
# .gitlab-ci.yml
include:
  - component: $CI_SERVER_FQDN/rkristelijn/keur/keur@v1
    inputs:
      args: check
```

### Inputs

Mirrored from the Action; authoritative list in
[`templates/keur.yml`](../templates/keur.yml):

| Input | Default | Purpose |
|-------|---------|---------|
| `version` | `latest` | keur release tag to install (→ `KEUR_VERSION`). Pin in CI. |
| `stage` | `test` | pipeline stage the keur job runs in |
| `args` | `check` | the keur subcommand + arguments to run |

```yaml
include:
  - component: $CI_SERVER_FQDN/rkristelijn/keur/keur@v1
    inputs:
      version: v0.2.0
      stage: test
      args: check --tier pre-push
```

## Pinning policy

The wrappers default to `version: latest`, but **CI should pin a tag**
(`version: v0.2.0`) for reproducible runs — the opposite of the local install
default, where latest is convenient. A pinned tag means a new keur release never
changes your pipeline's verdict without you bumping it.

## Configuration

Policy thresholds, per-tier scanner lists, and the tool inventory come from
`keur.toml`, resolved independently of the wrapper (built-in → global →
installed → explicit `--config`). To use a repo-local config in CI, pass it
through `args`:

```yaml
      - uses: rkristelijn/keur@v1
        with:
          args: check --config keur.toml
```

See the README "Configuration" section and `defaults/keur.toml` for the full set.

## Validate

Verify the wrapper works before relying on it. keur's own repo dogfoods both the
gate and the Action:

- `tests/e2e/test_action.sh` exercises the install.sh wrapper contract
  (fetch + checksum-verify → PATH → `keur check`/`scan`).
- The `action-dogfood` job in `.github/workflows/ci.yml` runs the Action by its
  local path (`uses: ./`) on every push/PR.

Run the wrapper test locally (needs network + a published release):

```bash
bash tests/e2e/test_action.sh
```

## More info

- [`action.yml`](../action.yml) — the GitHub composite Action (authoritative inputs)
- [`templates/keur.yml`](../templates/keur.yml) — the GitLab CI/CD component
- `docs/adr/ADR-062-ci-distribution-action-and-component.md` — why two thin
  wrappers over the binary, not a Docker image
- `docs/adr/ADR-012-pipeline-coverage.md` — keur as a pipeline stage
  (secret-scan / sast)
- `install.sh` — the OS/arch + checksum-verify contract both channels reuse
- README "Subcommands" — what `check`, `scan`, and the tiers do
