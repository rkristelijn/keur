#!/usr/bin/env bash
# smoke.sh — post-compile end-to-end smoke test of keur's essential subcommands.
#
# Runs the freshly built binary through the commands a user relies on
# (version, help, policy explain, scan, check) and asserts each exits sanely
# and prints the expected shape of output. Cross-platform: this same script
# runs on Linux, macOS and (through the MSYS2 shell) Windows in CI.
#
# Usage: bash tests/e2e/smoke.sh [path-to-keur-binary]
#        default: bin/keur   (CI passes bin/keur.exe on Windows)
set -o nounset
set -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
ROOT="$(cd "$HERE/../.." && pwd -P)"
BIN="${1:-$ROOT/bin/keur}"

# Resolve relative to repo root if a bare relative path was given.
case "$BIN" in
  /*) : ;;
  *)  BIN="$ROOT/$BIN" ;;
esac

if [[ ! -x "$BIN" ]]; then
  echo "ERROR: keur binary not found/executable at: $BIN"
  exit 1
fi

PASS=0
FAIL=0
check() {  # check "<label>" <expected_exit> "<must-contain>" -- <cmd...>
  local label="$1"; local want_exit="$2"; local needle="$3"; shift 4
  local out rc
  out="$("$@" 2>&1)"; rc=$?
  local ok=1
  [[ "$rc" -eq "$want_exit" ]] || ok=0
  if [[ -n "$needle" ]] && ! printf '%s' "$out" | grep -qF "$needle"; then ok=0; fi
  if [[ "$ok" -eq 1 ]]; then
    printf "  ok    %s\n" "$label"; PASS=$((PASS+1))
  else
    printf "  FAIL  %s (exit=%s, want=%s)\n" "$label" "$rc" "$want_exit"
    printf '%s\n' "$out" | sed 's/^/          /' | head -5
    FAIL=$((FAIL+1))
  fi
}

# keur is a LOCAL commit-hook, not a CI gate: some of its rules are properties
# of the git context the committer sits in, not of the binary. The clearest
# example is KEUR-BRANCH-001 (ADR-057) — "don't commit directly to the default
# branch" — which BLOCKS when `keur check` runs on `main` at a managed maturity
# level. keur's OWN repo is level 5, and CI checks `main` out as a real branch,
# so running `keur check` from the repo root makes the binary enforce keur's own
# branch discipline against the test harness and exit 1 (ADR-073). That is
# correct product behaviour, wrong thing to assert here: the `check (no stage)`
# smoke must verify a binary PROPERTY ("nothing staged -> PASS"), independent of
# which branch CI happens to check out. So we run it in a hermetic sandbox: a
# throwaway git repo on a feature branch, with HOME/XDG isolated so neither
# keur's own keur.toml nor the runner's global config leaks in.
SANDBOX=""
make_sandbox() {  # prints the sandbox repo path on stdout
  local sb; sb="$(mktemp -d)"
  SANDBOX="$sb"
  (
    cd "$sb" || exit 1
    git init -q -b feature/smoke-check
    git config user.email smoke@keur.test
    git config user.name  "keur smoke"
  )
  printf '%s' "$sb"
}
cleanup_sandbox() { [[ -n "$SANDBOX" && -d "$SANDBOX" ]] && rm -rf "$SANDBOX"; }
trap cleanup_sandbox EXIT

echo "==> keur smoke test ($BIN)"

# 1. version — prints a version string, exit 0.
check "version"          0 "" -- "$BIN" version
# 2. help — prints usage including the subcommand list, exit 0.
check "help"             0 "scan" -- "$BIN" help
# 3. policy explain — resolves and prints the policy thresholds, exit 0.
check "policy explain"   0 "block-at" -- "$BIN" policy explain
# 4. scan — runs the rule engine over a directory, exit 0 (scan never blocks).
check "scan (self)"      0 "" -- "$BIN" scan "$ROOT/rules"
# 5. check — with nothing staged it prints PASS and exits 0. Run in a hermetic
#    sandbox (feature branch + isolated HOME/XDG) so branch discipline and the
#    runner's global config cannot turn this binary-property check into a BLOCK.
SB="$(make_sandbox)"
check "check (no stage)" 0 "PASS" -- \
  env -i PATH="$PATH" HOME="$SB/home" XDG_CONFIG_HOME="$SB/home/.config" \
  sh -c 'cd "$1" && exec "$2" check' _ "$SB" "$BIN"

echo ""
echo "  Smoke: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
