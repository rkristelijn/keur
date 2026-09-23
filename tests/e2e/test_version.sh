#!/usr/bin/env bash
# test_version.sh — e2e: keur version, help, unknown command.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found (run make build)"; exit 1; }

echo "==> test_version ($BINARY)"

# version prints something non-empty
out="$("$BINARY" version 2>/dev/null)"
[[ -n "$out" ]] && ok "version prints output: $out" || fail "version printed nothing"

# help mentions the subcommands
help="$("$BINARY" help 2>/dev/null)"
echo "$help" | grep -q "check"  && ok "help mentions check"  || fail "help missing check"
echo "$help" | grep -q "scan"   && ok "help mentions scan"   || fail "help missing scan"
echo "$help" | grep -q "hook"   && ok "help mentions hook"   || fail "help missing hook"
echo "$help" | grep -q "policy" && ok "help mentions policy" || fail "help missing policy"

# no-arg prints usage and exits 0
"$BINARY" >/dev/null 2>&1 && ok "no-arg exits 0" || fail "no-arg non-zero exit"

# unknown command exits non-zero
"$BINARY" frobnicate >/dev/null 2>&1 && fail "unknown command should exit non-zero" || ok "unknown command exits non-zero"

summary
