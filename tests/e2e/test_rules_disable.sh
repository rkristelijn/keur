#!/usr/bin/env bash
# test_rules_disable.sh — e2e: keur rules disable/enable toggles the disable-list
# and the engine honours it. Runs in a throwaway git repo so no real config is
# touched.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_rules_disable ($BINARY)"

ROOT="$(repo_root)"
# a throwaway repo with its own rule dir + one rule + a file that triggers it.
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
git -C "$tmp" init -q
mkdir -p "$tmp/rules"
cat > "$tmp/rules/T-1.rule" <<'RULE'
id: T-DIS-001
title: demo pickle sink
category: sast
engine: pattern
target:
  extensions: .py
patterns:
  - regex: \bpickle\.loads\s*\(
    message: demo
confidence: 0.8
RULE
printf 'import pickle\npickle.loads(x)\n' > "$tmp/app.py"

export KEUR_RULES_DIRS="$tmp/rules"

# 1. baseline: rule fires
n=$(cd "$tmp" && "$BINARY" scan app.py 2>/dev/null | grep -c 'T-DIS-001')
[ "$n" -ge 1 ] && ok "baseline: rule fires ($n)" || fail "rule did not fire at baseline"

# 2. disable it (writes repo keur.toml in $tmp)
(cd "$tmp" && "$BINARY" rules disable T-DIS-001 --scope repo >/dev/null 2>&1)
[ -f "$tmp/keur.toml" ] && grep -q 'T-DIS-001' "$tmp/keur.toml" \
  && ok "disable wrote [rules] disabled to keur.toml" || fail "disable did not write config"

# 3. after disable: rule no longer fires
n=$(cd "$tmp" && "$BINARY" scan app.py 2>/dev/null | grep -c 'T-DIS-001')
[ "$n" -eq 0 ] && ok "after disable: rule is skipped" || fail "rule still fired after disable ($n)"

# 4. enable it again
(cd "$tmp" && "$BINARY" rules enable T-DIS-001 --scope repo >/dev/null 2>&1)
n=$(cd "$tmp" && "$BINARY" scan app.py 2>/dev/null | grep -c 'T-DIS-001')
[ "$n" -ge 1 ] && ok "after enable: rule fires again ($n)" || fail "rule stayed disabled after enable"

# 5. save keeps ONE backup: the second write copies the prior keur.toml to .bak.
#    (This is the same rules_toggle the `keur manager` TUI calls on `s`, so it
#    also proves the TUI's save-with-backup contract without needing a pty.)
(cd "$tmp" && "$BINARY" rules disable T-DIS-001 --scope repo >/dev/null 2>&1)   # write 1
(cd "$tmp" && "$BINARY" rules disable KEUR-SEC-001 --scope repo >/dev/null 2>&1) # write 2 → .bak
if [ -f "$tmp/keur.toml.bak" ]; then
  # The backup is the PREVIOUS version: has T-DIS-001, not yet KEUR-SEC-001.
  if grep -q 'T-DIS-001' "$tmp/keur.toml.bak" && ! grep -q 'KEUR-SEC-001' "$tmp/keur.toml.bak"; then
    ok "save keeps one backup with the previous version (keur.toml.bak)"
  else
    fail "keur.toml.bak exists but is not the previous version"
  fi
else
  fail "no keur.toml.bak written on the second save"
fi

# 6. manager --status reflects the resolved disable state (overlay reads the TOML).
out="$(cd "$tmp" && "$BINARY" manager --status 2>/dev/null)"
printf '%s' "$out" | grep -q 'Rules' \
  && ok "manager --status renders the rules overview" \
  || fail "manager --status did not render a rules section"

# 7. save creates a MISSING parent dir (regression: a fresh ~/.config/keur must
#    not make the write fail silently — the bug behind "my change didn't stick").
fresh="$(mktemp -d)"
XDG_CONFIG_HOME="$fresh/.config" HOME="$fresh" "$BINARY" rules disable KEUR-SEC-001 --scope global >/dev/null 2>&1
if [ -f "$fresh/.config/keur/keur.toml" ] && grep -q 'KEUR-SEC-001' "$fresh/.config/keur/keur.toml"; then
  ok "global save creates the missing config dir and writes"
else
  fail "global save did not create ~/.config/keur or write the file"
fi
rm -rf "$fresh"

summary
