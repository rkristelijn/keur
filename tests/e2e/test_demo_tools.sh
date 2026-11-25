#!/usr/bin/env bash
# test_demo_tools.sh — e2e: the demo safety tools fail closed and clean up.
# @proves ADR-047
#   check-pii.sh : flags any /home/<name> (not only lowercase) and FAILS when a
#                  requested file is missing or unreadable (never reports "clean").
#   sandbox.sh   : leaves no temp HOME behind and passes keur's exit status through.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_demo_tools ($BINARY)"

DEMO="$(repo_root)/docs/demo"
WORK="$(mktemp -d)"
trap 'chmod -R u+rw "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT

# --- check-pii.sh ---------------------------------------------------------
pii() { bash "$DEMO/check-pii.sh" "$@" >/dev/null 2>&1; echo $?; }

printf 'cd /home/Alice/project\n' > "$WORK/upper.txt"
[[ "$(pii "$WORK/upper.txt")" -ne 0 ]] && ok "check-pii flags /home/Alice" || fail "check-pii missed /home/Alice"

[[ "$(pii "$WORK/does-not-exist.txt")" -ne 0 ]] && ok "check-pii fails on a missing file" || fail "check-pii reported a missing file as clean"

printf 'fine\n' > "$WORK/locked.txt"; chmod 000 "$WORK/locked.txt"
if [[ -r "$WORK/locked.txt" ]]; then
  ok "check-pii unreadable case skipped (running as root)"
else
  [[ "$(pii "$WORK/locked.txt")" -ne 0 ]] && ok "check-pii fails on an unreadable file" || fail "check-pii reported an unreadable file as clean"
fi

printf 'cd /tmp/demo-x\n' > "$WORK/clean.txt"
[[ "$(pii "$WORK/clean.txt")" -eq 0 ]] && ok "check-pii passes a clean file" || fail "check-pii flagged a clean file"

# --- sandbox.sh -----------------------------------------------------------
tmp_count() { find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'tmp.*' -type d 2>/dev/null | wc -l | tr -d ' '; }
before="$(tmp_count)"
KEUR_BIN="$BINARY" bash "$DEMO/sandbox.sh" keur version >/dev/null 2>&1
rc=$?
after="$(tmp_count)"
[[ "$rc" -eq 0 ]] && ok "sandbox passes exit 0 through" || fail "sandbox keur version exit=$rc"
[[ "$after" -le "$before" ]] && ok "sandbox leaves no temp HOME behind" || fail "sandbox leaked a temp dir ($before -> $after)"

summary
