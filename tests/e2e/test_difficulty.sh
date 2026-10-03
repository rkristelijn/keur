#!/usr/bin/env bash
# test_difficulty.sh — e2e: the difficulty vector actually selects which rules
# run, per-ility and per-rule overrides take effect, and the cascade resolves.
# Uses `keur difficulty --explain --json` so assertions are deterministic.
# @proves DESIGN-DIFFRES.explain @proves DESIGN-DIFFRES.step.wire
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found (run make build)"; exit 1; }

echo "==> test_difficulty ($BINARY)"

# KEUR_HOME must point at the repo root so the full rule corpus loads.
KH="$(cd "$(dirname "$BINARY")/.." && pwd -P)"

# Work in a throwaway git repo so repo keur.toml resolves (and global is ignored
# for the assertions because repo [policy] level overrides it).
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
(cd "$tmp" && git init -q && printf 'let a=1;\n' > t.ts)

run_json() {  # $1 = keur.toml body -> prints the --explain --json line
  printf '%s' "$1" > "$tmp/keur.toml"
  (cd "$tmp" && KEUR_HOME="$KH" "$BINARY" difficulty --explain --json 2>/dev/null)
}
active_of() { echo "$1" | grep -oE '"active":[0-9]+' | grep -oE '[0-9]+'; }

# 1. CMMI-1: only maturity-1 rules active (far fewer than the whole corpus).
j1="$(run_json '[policy]
level = 1
')"
a1="$(active_of "$j1")"
[[ -n "$a1" && "$a1" -gt 0 && "$a1" -lt 300 ]] \
  && ok "level=1 activates a small deadly set ($a1 rules)" \
  || fail "level=1 unexpected active count: $a1"

# 2. level=1 + integrity=5 opens the integrity slider -> strictly MORE rules.
j2="$(run_json '[policy]
level = 1
[difficulty]
integrity = 5
')"
a2="$(active_of "$j2")"
[[ "$a2" -gt "$a1" ]] \
  && ok "integrity=5 opens more rules ($a1 -> $a2)" \
  || fail "integrity=5 did not raise active count ($a1 -> $a2)"

# 3. The resolved vector reflects the per-ility value.
echo "$j2" | grep -q '"integrity":5' \
  && ok "resolved vector shows integrity=5" \
  || fail "resolved vector missing integrity=5"

# 4. Per-rule override: SEC-038 off at a level where it would otherwise fire.
j3="$(run_json '[policy]
level = 1
[difficulty]
integrity = 2
[difficulty.rules]
SEC-038 = off
')"
why="$(cd "$tmp" && KEUR_HOME="$KH" "$BINARY" difficulty --why SEC-038 2>/dev/null)"
echo "$why" | grep -q "forced OFF" \
  && ok "SEC-038 override reported as forced OFF" \
  || fail "SEC-038 override not reflected in --why"
echo "$j3" | grep -q '"SEC-038":false' \
  && ok "override present in --json" \
  || fail "override missing in --json"

# 5. --why-not tells you what to change to activate a sleeping rule.
wn="$(cd "$tmp" && KEUR_HOME="$KH" "$BINARY" difficulty --why-not A11Y-001 2>/dev/null)"
echo "$wn" | grep -qiE "to ACTIVATE" \
  && ok "--why-not suggests how to activate" \
  || fail "--why-not gave no activation hint"

# 6. Cascade: repo level=1 overrides a higher global level (fewer rules than
#    an all-open level=5 body).
j5="$(run_json '[policy]
level = 5
')"
a5="$(active_of "$j5")"
[[ "$a5" -gt "$a1" ]] \
  && ok "level=5 activates more than level=1 ($a1 < $a5)" \
  || fail "level=5 not greater than level=1 ($a1 vs $a5)"

summary
