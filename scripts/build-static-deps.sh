#!/usr/bin/env bash
# build-static-deps.sh — build RE2 + Abseil from pinned source as STATIC libraries
# into a local prefix, so keur links them into its own binary (issue #36).
#
#   scripts/build-static-deps.sh [prefix]     default prefix: .deps/static
#
# Why: linking the package manager's shared libre2 made every release binary
# depend on that exact library (Homebrew dylib, libre2.so.10, libre2-11.dll). An
# Abseil upgrade on the host then crashed keur at startup. Static archives built
# from pinned, checksum-verified source make the binary self-contained.
#
# Idempotent: a prefix that already holds both archives for these versions is
# reused (CI caches it). Needs cmake and a C++17 compiler; nothing else.
# @implements ADR-078
set -euo pipefail

RE2_VERSION="2025-11-05"
RE2_SHA256="87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67"
ABSL_VERSION="20250814.1"  # keur:ignore VAULT-GDPR_NL — upstream Abseil LTS version, not a personal number
ABSL_SHA256="1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1"

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
PREFIX="${1:-$ROOT/.deps/static}"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd -P)"
STAMP="$PREFIX/.versions"
WANT="re2=$RE2_VERSION absl=$ABSL_VERSION"

if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$WANT" && -f "$PREFIX/lib/libre2.a" ]]; then
  echo "build-static-deps: up to date in $PREFIX ($WANT)"
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# fetch <url> <sha256> <file>: download and refuse a tarball whose checksum differs.
fetch() {
  curl -fsSL --retry 3 -o "$WORK/$3" "$1"
  local got
  got="$( (sha256sum "$WORK/$3" 2>/dev/null || shasum -a 256 "$WORK/$3") | awk '{print $1}')"
  if [[ "$got" != "$2" ]]; then
    echo "build-static-deps: checksum mismatch for $3 (got $got, want $2)" >&2
    exit 1
  fi
}

fetch "https://github.com/abseil/abseil-cpp/releases/download/$ABSL_VERSION/abseil-cpp-$ABSL_VERSION.tar.gz" \
      "$ABSL_SHA256" absl.tar.gz
fetch "https://github.com/google/re2/releases/download/$RE2_VERSION/re2-$RE2_VERSION.tar.gz" \
      "$RE2_SHA256" re2.tar.gz
tar -xzf "$WORK/absl.tar.gz" -C "$WORK"
tar -xzf "$WORK/re2.tar.gz" -C "$WORK"

# Static, position-independent, release-optimised; installed into one prefix.
common=(-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_CXX_STANDARD=17
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX"
        -DCMAKE_INSTALL_LIBDIR=lib)

cmake -S "$WORK/abseil-cpp-$ABSL_VERSION" -B "$WORK/absl-build" "${common[@]}" \
      -DABSL_PROPAGATE_CXX_STD=ON -DABSL_BUILD_TESTING=OFF >/dev/null
cmake --build "$WORK/absl-build" --parallel "$JOBS" >/dev/null
cmake --install "$WORK/absl-build" >/dev/null

cmake -S "$WORK/re2-$RE2_VERSION" -B "$WORK/re2-build" "${common[@]}" \
      -DRE2_BUILD_TESTING=OFF >/dev/null
cmake --build "$WORK/re2-build" --parallel "$JOBS" >/dev/null
cmake --install "$WORK/re2-build" >/dev/null

echo "$WANT" > "$STAMP"
echo "build-static-deps: built $WANT into $PREFIX ($(ls "$PREFIX"/lib/libabsl_*.a | wc -l | tr -d ' ') abseil archives)"
