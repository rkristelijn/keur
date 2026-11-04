#!/usr/bin/env bash
# scripts/formula.sh — generate the Homebrew formula for keur (ADR-045 step 4a).
#
# keur ships ONE bare prebuilt binary per platform (keur-<os>-<arch>), not a
# tarball — so the formula downloads the bare asset and installs it as `keur`.
# This is the only per-channel logic; everything it points at is the existing
# GitHub Release the one-click workflow already produces (ADR-002/ADR-045), and
# the sha256 values are the same ones install.sh verifies against SHA256SUMS.
#
# The workflow calls this after a release with the version + the three sha256
# sums read straight from the release's SHA256SUMS, so the formula never drifts
# from the artifact it describes.
#
# Usage: bash scripts/formula.sh <version> <sha_macos_arm64> <sha_linux_amd64>
#
# Note: keur publishes macos-arm64 and linux-amd64 today (install.sh falls back
# macOS amd64 → arm64 via Rosetta; there is no linux-arm64 asset yet). The
# formula mirrors exactly those assets, so it only claims what the release ships.
set -o errexit -o nounset -o pipefail

VERSION="${1:?need version}"
SHA_MACOS_ARM64="${2:?need macos-arm64 sha256}"
SHA_LINUX_AMD64="${3:?need linux-amd64 sha256}"

cat <<EOF
class Keur < Formula
  desc "Deterministic AI-code guardrail — an anti-slop, vibe-code limiter (git hook)"
  homepage "https://github.com/rkristelijn/keur"
  license "MIT"
  version "${VERSION}"

  on_macos do
    # No macOS amd64 asset: Intel Macs run the arm64 build under Rosetta, which
    # mirrors install.sh's own macos-amd64 → arm64 fallback.
    url "https://github.com/rkristelijn/keur/releases/download/v${VERSION}/keur-macos-arm64"
    sha256 "${SHA_MACOS_ARM64}"

    def install
      bin.install "keur-macos-arm64" => "keur"
    end
  end

  on_linux do
    url "https://github.com/rkristelijn/keur/releases/download/v${VERSION}/keur-linux-amd64"
    sha256 "${SHA_LINUX_AMD64}"

    def install
      bin.install "keur-linux-amd64" => "keur"
    end
  end

  test do
    assert_match "keur", shell_output("#{bin}/keur version")
  end
end
EOF
