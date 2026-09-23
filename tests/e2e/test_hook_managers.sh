#!/usr/bin/env bash
# test_hook_managers.sh — e2e: keur detects the common hook tools by signature.
# Delegates to scripts/make-hook-fixtures.sh, which spins up one throwaway repo
# per tool (husky, pre-commit, lefthook, overcommit, simple-git-hooks, raw
# .githooks/, lint-staged, commitlint, commitizen), drops that tool's signature
# file, and asserts `keur manager --status` detects it.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

ROOT="$(repo_root)"
echo "==> test_hook_managers ($BINARY)"

if bash "$ROOT/scripts/make-hook-fixtures.sh" "$BINARY"; then
  ok "all hook-tool fixtures detected"
else
  fail "one or more hook tools were not detected"
fi

summary
