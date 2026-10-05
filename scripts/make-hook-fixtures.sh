#!/usr/bin/env bash
# make-hook-fixtures.sh — create throwaway git repos, one per hook tool, drop
# that tool's signature file, and assert `keur manager --status` detects it.
#
# Proves keur's hook-manager/runner detection against the real signature files
# of the ~10 most common tools, without installing any of them. Each repo is
# isolated (own temp dir, own HOME/XDG so no global config leaks in) and removed
# afterwards. Portable: POSIX tools only (no grep -P, no realpath).
#
# Usage: make-hook-fixtures.sh [path-to-keur-binary]
set -euo pipefail

BIN="${1:-}"
if [[ -z "$BIN" ]]; then
  here="$(cd "$(dirname "$0")" && pwd -P)"
  BIN="$here/../bin/keur"
fi
[[ -x "$BIN" ]] || {
  echo "FAIL: keur binary not found ($BIN)"
  exit 1
}
BIN="$(cd "$(dirname "$BIN")" && pwd -P)/$(basename "$BIN")"

PASS=0
FAIL=0
ok() {
  PASS=$((PASS + 1))
  printf '  ok   %s\n' "$1"
}
bad() {
  FAIL=$((FAIL + 1))
  printf '  FAIL %s\n' "$1"
}

# tool | signature file (repo-relative) | expected name in `manager --status`
FIXTURES="
husky|.husky/pre-commit|husky
pre-commit|.pre-commit-config.yaml|pre-commit
lefthook|lefthook.yml|lefthook
overcommit|.overcommit.yml|overcommit
simple-git-hooks|.simple-git-hooks.json|simple-git-hooks
githooks|.githooks/pre-commit|githooks
lint-staged|.lintstagedrc|lint-staged
commitlint|commitlint.config.js|commitlint
commitizen|.czrc|commitizen
"

echo "==> make-hook-fixtures ($BIN)"

make_repo() { # $1 signature-relpath → echoes repo dir
  local sig="$1" d
  d="$(mktemp -d)"
  git -C "$d" init -q
  git -C "$d" config user.email "fx@keur.test"
  git -C "$d" config user.name "keur fx"
  # create the signature file (and any parent dir)
  mkdir -p "$d/$(dirname "$sig")"
  printf '# keur hook-fixture\n' >"$d/$sig"
  echo "$d"
}

fails="$(mktemp)"
trap 'rm -f "$fails"' EXIT
count=0

# Feed the loop without a pipe (process substitution) so PASS/FAIL live in this
# shell and the exit code is accurate.
while IFS='|' read -r tool sig expect; do
  [[ -n "$tool" ]] || continue
  count=$((count + 1))
  repo="$(make_repo "$sig")"
  out="$(cd "$repo" && env HOME="$repo/.home" XDG_CONFIG_HOME="$repo/.home/.config" \
    "$BIN" manager --status 2>/dev/null || true)"
  if printf '%s' "$out" | grep -q "$expect"; then
    ok "$tool detected (signature $sig)"
  else
    bad "$tool NOT detected (signature $sig)"
    echo x >>"$fails"
  fi
  rm -rf "$repo"
done <<EOF
$(printf '%s\n' "$FIXTURES")
EOF

nfail="$(wc -l <"$fails" | tr -d ' ')"
echo ""
echo "  Results: $((count - nfail)) passed, ${nfail} failed (of ${count} tools)"
[[ "$nfail" -eq 0 ]]
