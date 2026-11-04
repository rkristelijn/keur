#!/usr/bin/env bash
# check-pii.sh — fail if a demo tape or golden-output would leak PII into a
# rendered GIF or a committed snapshot (ADR-042 spirit, demo-side).
#
# WHY: a demo runs on a maintainer's machine. If a tape TYPES or a command
# PRINTS an absolute home path, the shell expands `$HOME` to `/Users/<name>` (or
# /home/<name>) and the username is burned into the GIF — and into any
# <name>.out.txt. This gate catches that BEFORE render/commit. Demos must use
# relative paths or a throwaway HOME (see sandbox.sh), never the real $HOME.
#
#   ./check-pii.sh            scan all *.tape + *.out.txt in this dir
#   ./check-pii.sh <file>...  scan specific files
#
# Exit 0 = clean; exit 1 = a leak (prints file:line). Meant for a CI demo gate.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
cd "$HERE"

# Patterns that mean "a real user's path/name could end up on screen":
#   - /Users/<something> or /home/<something>  (absolute home, OS-specific)
#   - a literal "$HOME/" or "~/" TYPED into a tape (expands to the real home
#     in the recording unless HOME is already a throwaway temp)
# RFC-safe demo placeholders (/tmp/demo, $CLEAN inside sandbox.sh) are allowed.
leak_re='(/Users/|/home/[a-z]|\$HOME/|~/)'

files=()
if [[ $# -gt 0 ]]; then
  files=("$@")
else
  shopt -s nullglob
  files=(*.tape *.out.txt)
  shopt -u nullglob
fi

rc=0
for f in "${files[@]}"; do
  [[ -f "$f" ]] || continue
  # sandbox.sh legitimately sets HOME=$CLEAN; it is the isolation tool, not a
  # demo surface — skip it. Everything else is scanned.
  [[ "$f" == "sandbox.sh" ]] && continue
  # Only inspect lines a GIF/txt would actually show: TYPE'd commands in tapes,
  # and every line of an .out.txt snapshot.
  if [[ "$f" == *.tape ]]; then
    hits="$(grep -nE '^[[:space:]]*Type ' "$f" | grep -nE "$leak_re" || true)"
  else
    hits="$(grep -nE "$leak_re" "$f" || true)"
  fi
  if [[ -n "$hits" ]]; then
    echo "check-pii: LEAK in $f —" >&2
    printf '%s\n' "$hits" | sed 's/^/    /' >&2
    rc=1
  fi
done

if [[ "$rc" -eq 0 ]]; then
  echo "check-pii: clean — no home paths / usernames in $( [[ $# -gt 0 ]] && echo 'the given files' || echo 'tapes or golden outputs' )."
fi
exit "$rc"
