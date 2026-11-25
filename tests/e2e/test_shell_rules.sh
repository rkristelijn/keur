#!/usr/bin/env bash
# test_shell_rules.sh — e2e: SH-QUAL-012 (BusyBox portability) and SH-QUAL-016
# (broken command substitution) fire on real problems and stay silent on valid code.
# @proves SH-QUAL-012 SH-QUAL-016
# Each case was a review finding: a false positive that made a correct script look broken.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

echo "==> test_shell_rules ($BINARY)"

# The rules exclude test/ paths, so the snippets live in a neutral temp dir.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# keur loads rules from KEUR_HOME (KEUR_RULES_DIRS only ADDS dirs), so pin this checkout.
unset KEUR_RULES_DIRS
export KEUR_HOME="$(repo_root)"

# hits <rule-id> <script line>: findings of that rule for a one-line script.
hits() {
  printf '#!/bin/sh\n%s\n' "$2" > "$WORK/s.sh"
  "$BINARY" scan "$WORK/s.sh" 2>/dev/null | grep -c "\"rule\":\"$1\""
}
expect() {  # expect <want: fire|silent> <rule> <label> <line>
  local n; n="$(hits "$2" "$4")"
  if [[ "$1" == fire ]]; then
    [[ "$n" -ge 1 ]] && ok "$2 fires: $3" || fail "$2 should fire: $3"
  else
    [[ "$n" -eq 0 ]] && ok "$2 silent: $3" || fail "$2 false positive: $3 (count=$n)"
  fi
}

# SH-QUAL-012: only grep, sed and cat live in /bin on BusyBox.
expect fire   SH-QUAL-012 "/usr/bin/grep (BusyBox: /bin/grep)" '/usr/bin/grep -q x file'
expect fire   SH-QUAL-012 "/usr/bin/sed"                       '/usr/bin/sed -n 1p file'
expect silent SH-QUAL-012 "/usr/bin/sort (BusyBox: /usr/bin)"  '/usr/bin/sort file'
expect silent SH-QUAL-012 "/usr/bin/awk (BusyBox: /usr/bin)"   "/usr/bin/awk '{print}' file"
expect silent SH-QUAL-012 "sort -V (supported by BusyBox)"     'printf "1.10\n1.9\n" | sort -V'
expect fire   SH-QUAL-012 "here-string (still bash-only)"      'read -r a <<< "$x"'

# SH-QUAL-016: a dropped '$(' leaves a dangling 'cmd ... )' in the test.
expect fire   SH-QUAL-016 "mangled substitution"   '[ -n "20 20 printf %s "" | tr -d " ")" ]'
expect silent SH-QUAL-016 "proper \$()"            "[ -n \"\$(printf %s \"\$VAR\" | tr -d ' ')\" ]"
expect silent SH-QUAL-016 "prefix + proper \$()"   "[ -f \"/tmp/\$(printf %s \"\$name\" | tr ' ' '_')\" ]"
expect silent SH-QUAL-016 "no pipe"                '[ -n "$VAR" ]'

summary
