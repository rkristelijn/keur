#!/usr/bin/env bash
# test_manager.sh — e2e: keur manager --status (aggregated read-only board).
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_manager ($BINARY)"

# text output: exits 0 and shows all four sections.
out="$("$BINARY" manager --status 2>&1)"
rc=$?
[[ "$rc" -eq 0 ]] && ok "manager --status exits 0" || fail "manager --status exit $rc"
echo "$out" | grep -q "Config scopes" && ok "shows config scopes" || fail "no config scopes"
echo "$out" | grep -q "Hooks:"        && ok "shows hooks"         || fail "no hooks section"
echo "$out" | grep -q "Scanners"      && ok "shows scanners"      || fail "no scanners section"
echo "$out" | grep -q "Rules"         && ok "shows rules"         || fail "no rules section"

# json output: single object with the expected top-level keys.
json="$("$BINARY" manager --status --json 2>/dev/null)"
echo "$json" | grep -q '^{' && ok "json is an object" || fail "json not an object"
echo "$json" | grep -q '"config_layers"' && ok "json has config_layers" || fail "no config_layers"
echo "$json" | grep -q '"hooks"'    && ok "json has hooks"    || fail "no hooks key"
echo "$json" | grep -q '"scanners"' && ok "json has scanners" || fail "no scanners key"
echo "$json" | grep -q '"rules"'    && ok "json has rules"    || fail "no rules key"

# unknown subcommand exits non-zero.
"$BINARY" manager bogus >/dev/null 2>&1 && fail "unknown manager subcommand should fail" \
  || ok "unknown manager subcommand exits non-zero"

summary
