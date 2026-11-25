#!/usr/bin/env bash
# test_demo_rules.sh — e2e: the shipped demo rule DEMO-NL-001 fires on Dutch
# prose and stays silent on English that shares a short word with Dutch.
# @proves DEMO-NL-001
# docs/demo/rules/ is the single source the dutch-in-docs tape copies, so this
# test also guards what the GIF shows.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_demo_rules ($BINARY)"

RULES="$(repo_root)/docs/demo/rules"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# scan_count <markdown text>: number of DEMO-NL-001 findings for that README.
scan_count() {
  printf '%s\n' "$1" > "$WORK/README.md"
  KEUR_RULES_DIRS="$RULES" "$BINARY" scan "$WORK/README.md" 2>/dev/null | grep -c '"rule":"DEMO-NL-001"'
}

n="$(scan_count 'Deze tool werkt niet zonder een config.')"
[[ "$n" -ge 1 ]] && ok "fires on Dutch prose" || fail "Dutch prose not flagged (count=$n)"

n="$(scan_count 'We met the requirements and shipped it.')"
[[ "$n" -eq 0 ]] && ok "silent on English 'met'" || fail "English 'met' flagged as Dutch (count=$n)"

n="$(scan_count 'This tool needs a config file. Follow the steps below.')"
[[ "$n" -eq 0 ]] && ok "silent on plain English" || fail "plain English flagged (count=$n)"

summary
