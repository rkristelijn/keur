#!/usr/bin/env bash
# test_keurignore_scope.sh — e2e: keur's own .keurignore must not switch off a
# security rule for the whole repo.
# @proves ADR-035
# A bare `SEC-BIDI-001` line once suppressed the Trojan-Source check in EVERY
# file, so a real bidi character in source passed the gate. The suppression must
# stay path-scoped: demo GIFs and the rule's own examples silent, source caught.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_keurignore_scope ($BINARY)"

ROOT="$(repo_root)"
REPO="$(make_temp_repo)"
trap 'rm -rf "$REPO"' EXIT
cd "$REPO" || exit 1
git config core.hooksPath /dev/null
git checkout -q -b feat/scope-test
mkdir -p docs/demo rules/security src home/.config .config
cp "$ROOT/.keurignore" .keurignore
cp "$ROOT/docs/demo/secret-in-code.gif" docs/demo/
cp "$ROOT/rules/security/SEC-BIDI-001-trojan-source-control-chars.rule" rules/security/
printf 'x = 1 # \xe2\x80\xae reversed\n' > src/probe.py     # U+202E in real source
printf '[policy]\nlevel = 5\n' > .config/keur.toml          # every rule active
git add -A

out="$(env -i PATH="$PATH" HOME="$REPO/home" XDG_CONFIG_HOME="$REPO/home/.config" \
  KEUR_HOME="$ROOT" "$BINARY" check 2>&1 | sed 's/\x1b\[[0-9;]*m//g')"
bidi="$(echo "$out" | grep 'SEC-BIDI-001 |')"

echo "$bidi" | grep -q 'src/probe.py' \
  && ok "bidi character in source is still caught" \
  || fail "SEC-BIDI-001 suppressed in source: .keurignore entry is too broad" "$out"
echo "$bidi" | grep -q 'secret-in-code.gif' \
  && fail "demo GIF not suppressed" || ok "demo GIF (binary asset) is suppressed"
echo "$bidi" | grep -q 'SEC-BIDI-001-trojan-source' \
  && fail "rule's own examples not suppressed" || ok "the rule's own example file is suppressed"
grep -qxE 'SEC-BIDI-001' "$ROOT/.keurignore" \
  && fail "bare SEC-BIDI-001 line in .keurignore (suppresses it everywhere)" \
  || ok "no bare SEC-BIDI-001 line in .keurignore"

summary
