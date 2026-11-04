# Installing keur

keur ships as a single prebuilt binary per platform, published to a GitHub
Release with a `SHA256SUMS` manifest (ADR-002). Every channel below installs
that **same** binary and verifies its checksum before trusting it (SCA-030) —
they differ only in how you invoke them. Pick whichever fits your setup.

> No compiler or language runtime is needed to *use* keur. The distribution
> channels (brew/npx/apt) are thin wrappers over the one Release artifact, not
> separate builds (ADR-045).

## Homebrew (macOS, Linuxbrew)

The formula lives in this repo (not a separate `homebrew-keur` tap), so tap it
by its URL, then install:

```bash
brew tap rkristelijn/keur https://github.com/rkristelijn/keur
brew install rkristelijn/keur/keur
```

Recent Homebrew refuses formulae from a non-official tap until you trust it; if
you see an "untrusted tap" error, run:

```bash
brew trust rkristelijn/keur     # one-time, then: brew install rkristelijn/keur/keur
```

This installs the `keur-<os>-<arch>` binary from the latest Release; Homebrew
verifies the formula's `sha256` against the download. Intel macOS runs the arm64
build under Rosetta (there is no separate amd64 asset), mirroring `install.sh`.
The formula lives at [`Formula/keur.rb`](../Formula/keur.rb) and is regenerated
from `SHA256SUMS` on every release by `scripts/formula.sh`.

## npx (Node ≥ 18, any OS)

```bash
npx keur check          # run without installing
npm install -g keur     # or install globally for a persistent `keur`
```

keur is a C++ binary, not a Node package — the npm package is a thin launcher
that **lazily** fetches + checksum-verifies the matching binary on first run and
caches it (no `postinstall`, so it works under npm, pnpm, bun, and yarn alike).
Source: [`packaging/npm/`](../packaging/npm). The cache dir honours
`$KEUR_CACHE_DIR` / `$XDG_CACHE_HOME`, falling back to `~/.cache/keur/`.

## apt / .deb (Debian, Ubuntu)

Download the `.deb` from the latest Release and install it:

```bash
# Replace <version> with the release you want.
curl -fsSLO https://github.com/rkristelijn/keur/releases/latest/download/keur_<version>_amd64.deb
sudo apt install ./keur_<version>_amd64.deb
```

The package installs the binary + bundled rules under `/usr/lib/keur` and puts a
`keur` symlink on `PATH` (`/usr/bin/keur`). keur derives its home from the
binary location (ADR-063), so the built-in rules load with no environment setup.
Only `linux-amd64` is published today. Built by
[`scripts/build-deb.sh`](../scripts/build-deb.sh); a full apt repo / PPA is out
of scope for now (ADR-045).

## curl install.sh (any POSIX shell)

The portable fallback — download the installer, read it, run it. It detects
OS/arch, fetches the matching binary, and verifies the checksum before
installing to `~/.local/bin` (no sudo):

```bash
curl -fsSLO https://raw.githubusercontent.com/rkristelijn/keur/main/install.sh
less install.sh && sh install.sh
```

Pin a version or change the target dir:

```bash
KEUR_VERSION=v0.2.0 KEUR_BIN_DIR="$HOME/.local/bin" sh install.sh
```

## After installing

Install the git hook so every commit is gated:

```bash
keur hook --install          # this repo's .git/hooks
keur hook --global           # every repo on the machine
```

## CI pipelines

To run keur as a CI stage (GitHub Action / GitLab component) instead of a local
install, see the CI integration guide and `docs/adr/ADR-062-ci-distribution-action-and-component.md`.

## More info

- `install.sh` — the OS/arch + checksum-verify contract every channel reuses
- `docs/adr/ADR-045-go-public-distribution-cpm-decommission.md` — the channel
  sequence (brew → npx → apt) and why
- `docs/adr/ADR-002-single-cpp-binary.md` — one artifact, no runtime dependency
- `docs/adr/ADR-063-derive-keur-home.md` — why the .deb layout is `/usr/lib/keur`
- README "Local setup" — the quick-start
