#!/usr/bin/env bash
# test_config.sh — e2e: keur config list (read-only layer stack).
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_config ($BINARY)"

# text output: exits 0, shows the defaults layer + a header.
out="$("$BINARY" config list 2>&1)"
rc=$?
[ "$rc" -eq 0 ] && ok "config list exits 0" || fail "config list exit $rc"
echo "$out" | grep -q "defaults" && ok "lists the defaults layer" || fail "no defaults layer in output"
echo "$out" | grep -q "layers"   && ok "prints the layer header"  || fail "no header in output"

# json output: valid, an array, and includes the defaults scope.
json="$("$BINARY" config list --json 2>/dev/null)"
echo "$json" | grep -q '^\[' && ok "json output is an array" || fail "json not an array"
echo "$json" | grep -q '"scope":"defaults"' && ok "json includes defaults scope" \
  || fail "json missing defaults scope"

# unknown subcommand exits non-zero.
"$BINARY" config nonsense >/dev/null 2>&1 && fail "unknown config subcommand should fail" \
  || ok "unknown config subcommand exits non-zero"

summary
