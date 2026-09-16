#!/usr/bin/env bash
# test_scan.sh — e2e: keur scan emits JSONL findings over the fixtures dir.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

RULES="$(repo_root)/rules"
FIX="$(repo_root)/tests/fixtures"
export KEUR_RULES_DIRS="$RULES"

echo "==> test_scan ($BINARY)"

# Scanning the secrets fixture dir should emit KEUR-SEC-001 findings on stdout
out="$("$BINARY" scan "$FIX/secrets" 2>/dev/null)"
echo "$out" | grep -q "KEUR-SEC-001" \
  && ok "scan emits KEUR-SEC-001 for secrets fixtures" || fail "no KEUR-SEC-001 in scan output"

# Output must be valid JSONL (each non-empty line starts with {)
bad=0
while IFS= read -r line; do
  [ -z "$line" ] && continue
  case "$line" in {*) ;; *) bad=1 ;; esac
done <<< "$out"
[ "$bad" -eq 0 ] && ok "scan output is JSONL" || fail "scan output not valid JSONL"

# Scanning the clean fixture dir should emit no KEUR-SEC-001
clean_out="$("$BINARY" scan "$FIX/clean" 2>/dev/null)"
echo "$clean_out" | grep -q "KEUR-SEC-001" \
  && fail "clean dir should not emit KEUR-SEC-001" || ok "clean dir emits no KEUR-SEC-001"

summary
