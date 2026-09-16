#!/usr/bin/env bash
# test_policy.sh — e2e: keur policy explain.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_policy ($BINARY)"

out="$("$BINARY" policy explain 2>&1)"
rc=$?
[ "$rc" -eq 0 ] && ok "policy explain exits 0" || fail "policy explain exit $rc"
echo "$out" | grep -q "block-at" && ok "explain shows block-at" || fail "no block-at in output"
echo "$out" | grep -q "warn-at"  && ok "explain shows warn-at"  || fail "no warn-at in output"

# unknown policy subcommand exits non-zero
"$BINARY" policy nonsense >/dev/null 2>&1 && fail "unknown policy subcommand should fail" || ok "unknown policy subcommand exits non-zero"

summary
