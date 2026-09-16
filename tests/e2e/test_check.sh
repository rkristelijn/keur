#!/usr/bin/env bash
# keur:ignore-file KEUR-SEC-001 — e2e test: intentionally stages a fake key.
# test_check.sh — e2e: keur check gates a staged private key in a real git repo.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

RULES="$(repo_root)/rules"
export KEUR_RULES_DIRS="$RULES"

echo "==> test_check ($BINARY)"

REPO="$(make_temp_repo)"
trap 'rm -rf "$REPO"' EXIT

# 1. Clean staged file → PASS (exit 0)
(
  cd "$REPO"
  printf 'host: localhost\nport: 5432\n' > config.yml
  git add config.yml
)
(cd "$REPO" && "$BINARY" check --tier pre-commit >/dev/null 2>&1) \
  && ok "clean staged file → PASS (exit 0)" || fail "clean file should PASS"

# 2. Staged private key → BLOCK (exit 1)
(
  cd "$REPO"
  printf -- '-----BEGIN RSA PRIVATE KEY-----\nFAKEKEYMATERIAL0000000000000000\n-----END RSA PRIVATE KEY-----\n' > id_rsa.pem
  git add id_rsa.pem
)
(cd "$REPO" && "$BINARY" check --tier pre-commit >/dev/null 2>&1) \
  && fail "private key should BLOCK (exit 1)" || ok "staged private key → BLOCK (exit 1)"

# 3. No staged files → PASS
(
  cd "$REPO"
  git reset -q HEAD . 2>/dev/null || true
  git rm --cached -q config.yml id_rsa.pem 2>/dev/null || true
)
(cd "$REPO" && "$BINARY" check --tier pre-commit >/dev/null 2>&1) \
  && ok "no staged files → PASS" || fail "empty stage should PASS"

summary
