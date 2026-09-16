#!/usr/bin/env bash
# test_hook.sh — e2e: keur hook install/status/remove in an isolated git repo.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_hook ($BINARY)"

REPO="$(make_temp_repo)"
trap 'rm -rf "$REPO"' EXIT

(
  cd "$REPO"
  "$BINARY" hook --install >/dev/null 2>&1
)

# hooks land in .git/hooks
[ -x "$REPO/.git/hooks/pre-commit" ] && ok "pre-commit installed" || fail "pre-commit missing"
[ -x "$REPO/.git/hooks/pre-push" ]   && ok "pre-push installed"   || fail "pre-push missing"

# stub content
grep -q "exec keur check" "$REPO/.git/hooks/pre-commit" \
  && ok "pre-commit stub calls keur check" || fail "pre-commit stub wrong"
grep -q "pre-push" "$REPO/.git/hooks/pre-push" \
  && ok "pre-push stub references pre-push tier" || fail "pre-push stub wrong"

# status runs
(cd "$REPO" && "$BINARY" hook --status >/dev/null 2>&1) \
  && ok "hook --status exits 0" || fail "hook --status failed"

# remove
(cd "$REPO" && "$BINARY" hook --remove >/dev/null 2>&1)
[ ! -e "$REPO/.git/hooks/pre-commit" ] && ok "pre-commit removed" || fail "pre-commit still present"
[ ! -e "$REPO/.git/hooks/pre-push" ]   && ok "pre-push removed"   || fail "pre-push still present"

# install outside a git repo fails
( cd /tmp && "$BINARY" hook --install >/dev/null 2>&1 ) \
  && fail "install outside repo should fail" || ok "install outside repo exits non-zero"

summary
