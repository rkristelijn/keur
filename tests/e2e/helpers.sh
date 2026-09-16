#!/usr/bin/env bash
# helpers.sh — shared helpers for keur e2e tests.
# Sourced by every test_*.sh. Provides: resolve_binary, git identity setup,
# pass/fail counters, and coloured output.

GREEN='\033[32m'; RED='\033[31m'; YEL='\033[33m'; DIM='\033[90m'; R='\033[0m'
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf "  ${GREEN}ok${R}   %s\n" "$1"; }
fail() { FAIL=$((FAIL+1)); printf "  ${RED}FAIL${R} %s\n" "$1"; [ -n "${2:-}" ] && echo "         $2"; }

# Resolve the keur binary: arg, then repo bin/, then PATH.
resolve_binary() {
  local cand="${1:-}"
  if [ -n "$cand" ] && [ -x "$cand" ]; then
    (cd "$(dirname "$cand")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$cand")")
    return
  fi
  local here; here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
  local repo; repo="$(cd "$here/../.." && pwd -P)"
  if [ -x "$repo/bin/keur" ]; then echo "$repo/bin/keur"; return; fi
  command -v keur 2>/dev/null || echo ""
}

# Absolute path to the repo root (two levels up from tests/e2e/).
repo_root() {
  local here; here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
  (cd "$here/../.." && pwd -P)
}

# Create an isolated temp git repo and echo its path. Caller cleans up.
make_temp_repo() {
  local d; d="$(mktemp -d)"
  (
    cd "$d"
    git init -q
    git config user.email "e2e@keur.test"
    git config user.name "keur e2e"
  )
  echo "$d"
}

# Print the suite result line and set exit code.
summary() {
  echo ""
  echo "  Results: ${PASS} passed, ${FAIL} failed"
  [ "$FAIL" -eq 0 ]
}
