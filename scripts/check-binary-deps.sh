#!/usr/bin/env bash
# check-binary-deps.sh — fail when a keur binary depends on RE2 or Abseil at runtime.
#
#   scripts/check-binary-deps.sh <binary>...
#
# A release binary must be self-contained (issue #36): RE2 and Abseil linked in
# statically, so a host without them, or with a newer Abseil, still runs keur.
# This reads the binary's dynamic-library list with the platform tool and fails on
# any re2/absl entry. Prints the full list either way, so a CI log shows what the
# binary actually needs.
# @implements ADR-078
set -euo pipefail

[[ $# -gt 0 ]] || { echo "usage: $0 <binary>..." >&2; exit 2; }

# deps <binary>: one dynamic dependency per line.
deps() {
  case "$(uname -s)" in
    Darwin) otool -L "$1" | tail -n +2 | awk '{print $1}' ;;
    Linux)  readelf -d "$1" | sed -nE 's/.*\(NEEDED\).*\[(.*)\].*/\1/p' ;;
    MINGW*|MSYS*|CYGWIN*) objdump -p "$1" | sed -nE 's/^[[:space:]]*DLL Name: (.*)$/\1/p' ;;
    *) echo "check-binary-deps: unsupported OS $(uname -s)" >&2; return 2 ;;
  esac
}

rc=0
for bin in "$@"; do
  [[ -f "$bin" ]] || { echo "check-binary-deps: no such file: $bin" >&2; rc=1; continue; }
  list="$(deps "$bin")" || { rc=1; continue; }
  echo "check-binary-deps: $bin needs:"
  printf '%s\n' "$list" | sed 's/^/    /'
  bad="$(printf '%s\n' "$list" | grep -iE 're2|absl' || true)"
  if [[ -n "$bad" ]]; then
    echo "check-binary-deps: FAIL — $bin loads RE2/Abseil dynamically:" >&2
    printf '%s\n' "$bad" | sed 's/^/    /' >&2
    echo "  build it with: scripts/build-static-deps.sh && make -C src/keur build STATIC_RE2=\$PWD/.deps/static" >&2
    rc=1
  fi
done
exit "$rc"
