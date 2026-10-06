#!/usr/bin/env sh
# install.sh — install keur from a GitHub Release, with checksum verification.
#
# Secure by design (keur gates this pattern itself: SCA-030/063): it downloads
# the release binary AND SHA256SUMS, verifies the hash BEFORE installing, and
# never pipes an unverified download straight into a shell. It installs to
# ~/.local/bin (no sudo).
#
# Usage:
#   # Recommended: download, read it, then run.
#   curl -fsSLO https://raw.githubusercontent.com/rkristelijn/keur/main/install.sh
#   less install.sh && sh install.sh
#
#   # Pin a version / change dir:
#   KEUR_VERSION=v0.1.0 KEUR_BIN_DIR="$HOME/.local/bin" sh install.sh
#
# The binary itself is checksum-verified regardless of how you fetched this
# script, so a tampered binary is rejected even if the script delivery was not
# inspected.
set -eu

REPO="rkristelijn/keur"
VERSION="${KEUR_VERSION:-latest}"
BIN_DIR="${KEUR_BIN_DIR:-$HOME/.local/bin}"

die() { echo "install: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# Pick a downloader.
if have curl; then DL="curl -fsSL -o";
elif have wget; then DL="wget -qO";
else die "need curl or wget"; fi

# Detect OS/arch → release asset name (matches the Release workflow).
os="$(uname -s)"; arch="$(uname -m)"
case "$os" in
  Linux)  o="linux" ;;
  Darwin) o="macos" ;;
  *) die "unsupported OS: $os (use the Windows .exe asset manually)" ;;
esac
case "$arch" in
  x86_64|amd64) a="amd64" ;;
  arm64|aarch64) a="arm64" ;;
  *) die "unsupported arch: $arch" ;;
esac

# keur publishes keur-<os>-<arch>; macOS is arm64, Linux is amd64 today.
asset="keur-${o}-${a}"
case "$o-$a" in
  macos-amd64) asset="keur-macos-arm64"; echo "install: no macOS amd64 build; using arm64 (Rosetta)." >&2 ;;
  linux-arm64) die "no linux-arm64 release asset yet" ;;
esac

# Resolve the release base URL.
if [ "$VERSION" = "latest" ]; then
  base="https://github.com/$REPO/releases/latest/download"
else
  base="https://github.com/$REPO/releases/download/$VERSION"
fi

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "install: downloading $asset ($VERSION)…"
$DL "$tmp/$asset" "$base/$asset" || die "download failed: $base/$asset"
$DL "$tmp/SHA256SUMS" "$base/SHA256SUMS" || die "download failed: $base/SHA256SUMS"

# Verify the checksum BEFORE trusting the binary (the SCA-030 mitigation).
echo "install: verifying checksum…"
expected="$(grep " $asset\$" "$tmp/SHA256SUMS" | awk '{print $1}')"
[ -n "$expected" ] || die "no checksum for $asset in SHA256SUMS"
if have sha256sum; then
  actual="$(sha256sum "$tmp/$asset" | awk '{print $1}')"
elif have shasum; then
  actual="$(shasum -a 256 "$tmp/$asset" | awk '{print $1}')"
else
  die "need sha256sum or shasum to verify the download"
fi
[ "$actual" = "$expected" ] || die "checksum MISMATCH — refusing to install (expected $expected, got $actual)"

# Install.
mkdir -p "$BIN_DIR"
install -m 0755 "$tmp/$asset" "$BIN_DIR/keur"
echo "install: keur → $BIN_DIR/keur (checksum OK)"

# PATH hint.
case ":$PATH:" in
  *":$BIN_DIR:"*) : ;;
  *) echo "install: NOTE — $BIN_DIR is not on your PATH. Add it:"; echo "    export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac

"$BIN_DIR/keur" version >/dev/null 2>&1 && echo "install: done — $("$BIN_DIR/keur" version)" || echo "install: done."
