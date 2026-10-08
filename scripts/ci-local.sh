#!/usr/bin/env bash
# ci-local.sh — run the CI stages locally, in order, before pushing.
#
# Mirrors the stage ordering proposed in ADR-050 (cheap -> expensive, fail-fast)
# so a push does not become a debug-by-CI loop. This runs the Linux-equivalent
# of every stage that CAN run on this machine; the cross-platform matrix
# (macOS/Windows/arm32) is the one thing only real CI can prove, so it is listed
# but skipped here.
#
# Usage:  bash scripts/ci-local.sh [--fast]
#   --fast   skip e2e (unit/integration + lint + dogfood only)
#
# Exit non-zero on the first failing stage (fail-fast), like CI.
set -o nounset
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"

FAST=0
[ "${1:-}" = "--fast" ] && FAST=1

GREEN='\033[32m'; RED='\033[31m'; YEL='\033[33m'; DIM='\033[2m'; NC='\033[0m'
stage() { printf "\n${DIM}==>${NC} %s\n" "$1"; }
ok()    { printf "  ${GREEN}ok${NC}   %s\n" "$1"; }
skip()  { printf "  ${YEL}skip${NC} %s\n" "$1"; }
die()   { printf "  ${RED}FAIL${NC} %s\n" "$1"; exit 1; }

# Run a command, fail the stage if it exits non-zero. Output shown only on fail.
run() { # run "<label>" <cmd...>
  local label="$1"; shift
  if out="$("$@" 2>&1)"; then ok "$label"; else
    printf '%s\n' "$out" | sed 's/^/      /' | tail -25
    die "$label"
  fi
}

# ── Stage 1: lint (cheap, no build) ─────────────────────────────────────────
stage "stage 1 — lint (shell + docs)"
if command -v shellcheck >/dev/null 2>&1; then
  # Lint the e2e + scripts at error severity, matching what the ADR-050 lint
  # stage will enforce once it lands.
  run "shellcheck tests/e2e + scripts" bash -c '
    shellcheck -S error tests/e2e/*.sh scripts/*.sh'
else
  skip "shellcheck not installed (brew install shellcheck)"
fi
if command -v rumdl >/dev/null 2>&1; then
  # Advisory until the docs are brought under a rumdl baseline/config: the repo
  # has ~380 pre-existing MD013/MD032 issues across 66 files, so a hard fail here
  # would block every pre-flight on debt that is not yours. Report, do not fail.
  # (ADR-050 lint stage should add a rumdl config + a one-time cleanup, then flip
  # this to a hard gate.)
  if out="$(rumdl check docs 2>&1)"; then
    ok "rumdl docs"
  else
    n="$(printf '%s' "$out" | grep -oE 'Found [0-9]+ issues' | head -1)"
    skip "rumdl docs — ${n:-issues} (advisory; pre-existing debt, see ADR-050)"
  fi
else
  skip "rumdl not installed — docs lint"
fi

# ── Stage 2: build + unit/integration (the fast correctness gate) ───────────
stage "stage 2 — build + unit/integration"
run "make build"            make -C src/keur build
run "make test-unit"        make -C src/keur test-unit
run "make test-integration" make -C src/keur test-integration

# ── Stage 3: e2e (behavioural, reuse the built binary) ──────────────────────
if [ "$FAST" -eq 1 ]; then
  skip "stage 3 — e2e (--fast)"
else
  stage "stage 3 — e2e"
  run "make e2e" make -C src/keur e2e
fi

# ── Stage 4: dogfood (keur gates its own repo + diff) ───────────────────────
stage "stage 4 — dogfood"
run "self-check (architecture)" bash tests/e2e/test_selfcheck.sh bin/keur
run "self-check (portability)"  bash tests/e2e/test_portability.sh bin/keur
# The real gate the hook runs: keur check over the staged diff. Only meaningful
# when something is staged; otherwise it is a clean PASS.
run "keur check (pre-commit)" bin/keur check --tier pre-commit
run "keur check (pre-push)"   bin/keur check --tier pre-push

# ── Stage 5: cross-platform — only real CI can prove this ───────────────────
stage "stage 5 — cross-platform matrix (macOS/Windows/arm32)"
skip "only GitHub Actions can run the full OS matrix + QEMU arm32"

printf "\n${GREEN}All local stages passed.${NC} Cross-platform matrix still runs in CI.\n"
