#!/usr/bin/env bash
# build-deb.sh — build a keur .deb from the prebuilt Linux release binary
# (ADR-045 step 4c, apt/deb channel).
#
# keur derives its home from the binary location (ADR-063): <home>/bin/keur with
# <home>/rules/ beside it. So the package installs into /usr/lib/keur (binary +
# rules together) and symlinks /usr/bin/keur -> /usr/lib/keur/bin/keur. Because
# ADR-063 resolves the exe via realpath, the symlink is followed and the home is
# correctly derived as /usr/lib/keur — the built-in rules load with no env setup.
#
# This produces a single .deb to attach to the GitHub Release; a user installs
# with `sudo apt install ./keur_<version>_amd64.deb`. A full apt repo / PPA is
# intentionally out of scope for v1 (ADR-045: apt is "most upkeep", do it last).
#
# Usage:
#   scripts/build-deb.sh <version> <path-to-linux-binary> [<rules-dir>] [<out-dir>]
# Example (in CI, after the linux-amd64 build):
#   scripts/build-deb.sh 0.2.0 dist/keur-linux-amd64 rules dist
set -o errexit -o nounset -o pipefail

VERSION="${1:?need version (e.g. 0.2.0)}"
BINARY="${2:?need path to the linux keur binary}"
RULES_DIR="${3:-rules}"
OUT_DIR="${4:-dist}"
ARCH="amd64"

[ -f "$BINARY" ] || { echo "build-deb: binary not found: $BINARY" >&2; exit 1; }
[ -d "$RULES_DIR" ] || { echo "build-deb: rules dir not found: $RULES_DIR" >&2; exit 1; }
command -v dpkg-deb >/dev/null 2>&1 || { echo "build-deb: dpkg-deb not found (Debian/Ubuntu only)" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

PKGROOT="$STAGE/keur_${VERSION}_${ARCH}"
HOME_DIR="$PKGROOT/usr/lib/keur"

# Layout: binary + rules together under /usr/lib/keur (so ADR-063 derivation
# finds <home>/rules/ next to <home>/bin/keur), with a /usr/bin symlink on PATH.
mkdir -p "$HOME_DIR/bin" "$PKGROOT/usr/bin" "$PKGROOT/DEBIAN"
install -m 0755 "$BINARY" "$HOME_DIR/bin/keur"
cp -R "$RULES_DIR" "$HOME_DIR/rules"
ln -s /usr/lib/keur/bin/keur "$PKGROOT/usr/bin/keur"

# Control file. Architecture is amd64 (keur ships only linux-amd64 today).
cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: keur
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Maintainer: rkristelijn <noreply@github.com>
Homepage: https://github.com/rkristelijn/keur
Description: Deterministic AI-code guardrail (anti-slop, vibe-code limiter)
 keur is a single compiled C++ binary that runs as a git pre-commit/pre-push
 gate: staged files -> RE2 rule engine -> findings -> deterministic policy ->
 PASS/WARN/BLOCK. This package installs the binary and the bundled rule set.
EOF

mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/keur_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKGROOT" "$DEB" >/dev/null
echo "build-deb: wrote $DEB"
dpkg-deb --info "$DEB" | sed 's/^/  /'
