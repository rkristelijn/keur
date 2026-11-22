#!/usr/bin/env bash
# test_branch_suppression.sh — e2e: which suppression actually silences KEUR-BRANCH-001.
# @proves KEUR-BRANCH-001
#
# KEUR-BRANCH-001 has no source line: `keur check` adds it after the file scan, so an
# inline `keur:ignore` marker in a staged file cannot reach it. Only repo-level
# suppression applies: `.keurignore` or keur.toml `[rules] disabled`. This test pins
# that contract so the docs and the finding's own message cannot drift from it again.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_branch_suppression ($BINARY)"

REPO="$(make_temp_repo)"
trap 'rm -rf "$REPO"' EXIT
cd "$REPO" || exit 1
git config core.hooksPath /dev/null
git checkout -q -b main
mkdir -p .config home/.config
# Level 4 (managed): branch protection BLOCKS a direct commit to the default branch.
printf '[policy]\nlevel = 4\n' > .config/keur.toml
git add .config/keur.toml && git commit -q -m "chore: init"

run_check() {
  env -i PATH="$PATH" HOME="$REPO/home" XDG_CONFIG_HOME="$REPO/home/.config" \
    "$BINARY" check 2>&1
}

# assert_suppressed <label>: the gate RAN and let the commit through, and the
# branch finding is gone. Absence alone is not proof: a crash or empty output has
# no KEUR-BRANCH-001 either. The bare test repo has unrelated hygiene warnings,
# so the valid verdict here is PASS or WARN (both exit 0); BLOCK or no verdict
# line at all is a failure.
assert_suppressed() {
  local label="$1" verdict
  verdict="$(echo "$out" | grep -oE 'KEUR \(pre-commit\): (PASS|WARN|BLOCK)' | tail -1 | awk '{print $3}')"
  if [[ "$rc" -ne 0 ]]; then
    fail "$label: gate exited $rc (want 0)" "$out"
  elif [[ -z "$verdict" ]]; then
    fail "$label: no verdict line, the gate did not complete" "$out"
  elif [[ "$verdict" == "BLOCK" ]]; then
    fail "$label: verdict BLOCK (want PASS or WARN)" "$out"
  elif echo "$out" | grep -q "KEUR-BRANCH-001"; then
    fail "$label: KEUR-BRANCH-001 still reported" "$out"
  else
    ok "$label suppresses the branch finding (verdict $verdict, exit 0)"
  fi
}

# Baseline: on main, with a staged file, the branch finding must block.
echo "x = 1" > a.py && git add a.py
out="$(run_check)"; rc=$?
echo "$out" | grep -q "KEUR-BRANCH-001" && [[ $rc -eq 1 ]] \
  && ok "baseline: direct commit to main is blocked" \
  || fail "baseline: KEUR-BRANCH-001 did not block (rc=$rc)" "$out"

# 1. Inline keur:ignore in a staged file does NOT suppress it (no source line).
echo "x = 1  # keur:ignore KEUR-BRANCH-001" > a.py && git add a.py
out="$(run_check)"; rc=$?
echo "$out" | grep -q "KEUR-BRANCH-001" && [[ $rc -eq 1 ]] \
  && ok "inline keur:ignore does not suppress the branch finding" \
  || fail "inline keur:ignore unexpectedly suppressed KEUR-BRANCH-001 (rc=$rc)" "$out"

# 2. .keurignore suppresses it.
echo "x = 1" > a.py && git add a.py
echo "KEUR-BRANCH-001" > .keurignore
out="$(run_check)"; rc=$?
assert_suppressed ".keurignore"
rm -f .keurignore

# 3. keur.toml [rules] disabled suppresses it.
printf '[policy]\nlevel = 4\n\n[rules]\ndisabled = ["KEUR-BRANCH-001"]\n' > .config/keur.toml
out="$(run_check)"; rc=$?
assert_suppressed "[rules] disabled"

summary
