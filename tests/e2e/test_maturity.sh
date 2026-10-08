#!/usr/bin/env bash
# test_maturity.sh — keur holds ITSELF to the strictest maturity band and stays
# there (dogfooding). ADR-026 defines a 1..5 scale; keur's own keur.toml pins
# every ility to 5 ("optimising"). This check fails if the resolved difficulty
# vector drops below 5 on any ility — so "get to level 5 and stay" is enforced
# in CI, not just intended.
#
# @see ADR-026 (maturity levels), ADR-011 (project profile), keur.toml [difficulty]
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

ROOT="$(repo_root)"
echo "==> test_maturity ($BINARY)"

# Resolve keur's own difficulty vector from its repo-root keur.toml.
vector="$(cd "$ROOT" && "$BINARY" difficulty 2>/dev/null)"

# 1. Every resolved ility must be at level 5. Parse the "resolved vector" block
#    (lines "    <ility>   <level>") and flag any level below 5.
below="$(printf '%s' "$vector" \
  | awk '/resolved vector:/{f=1;next} /active rules:/{f=0} f && NF>=2 {lvl=$NF; if (lvl ~ /^[0-9]+$/ && lvl < 5) print}')"
if [[ -z "$below" ]]; then
  ok "every ility resolves to level 5 (strictest band, ADR-026)"
else
  fail "keur's own gate dropped below level 5 — restore [difficulty] = 5 in keur.toml" \
       "$(printf '%s' "$below" | tr '\n' ';')"
fi

# 2. Prove the check actually detects a drop: a vector with a sub-5 line must be
#    caught by the same parser (guards against a dead/always-pass check).
probe_below="$(printf 'resolved vector:\n    integrity          3\n  active rules: 1 / 1\n' \
  | awk '/resolved vector:/{f=1;next} /active rules:/{f=0} f && NF>=2 {lvl=$NF; if (lvl ~ /^[0-9]+$/ && lvl < 5) print}')"
[[ -n "$probe_below" ]] \
  && ok "detector catches a sub-5 ility (not a dead check)" \
  || fail "detector did NOT catch an injected sub-5 ility — check is dead"

summary
