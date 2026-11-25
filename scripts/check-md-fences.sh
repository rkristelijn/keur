#!/usr/bin/env bash
# check-md-fences.sh — fail when a Markdown code fence is closed with an info
# string (e.g. a closing ```text). CommonMark treats such a line as a NEW opening
# fence, so the block never closes and every heading after it renders as code.
#
#   scripts/check-md-fences.sh            check every tracked *.md
#   scripts/check-md-fences.sh <file>...  check only these files
#
# Why a script and not a .rule: whether a fence line opens or closes depends on
# the parity of every fence above it. A per-line regex (or a fixed window) cannot
# know that (ADR-040, engine coverage gaps), so a rule would only guess. This is
# the small deterministic check instead, run in CI next to the ADR-index check.
# Nested examples are fine: an outer fence of four backticks contains inner
# three-backtick blocks, exactly as CommonMark specifies.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"

if [[ $# -gt 0 ]]; then
  files=("$@")
else
  files=()
  while IFS= read -r f; do files+=("$f"); done < <(git ls-files '*.md' | grep -v '^vendor/')
fi

bad=0
for f in "${files[@]}"; do
  [[ -f "$f" ]] || { echo "check-md-fences: no such file: $f" >&2; bad=1; continue; }
  # awk tracks the open fence (char + length). A line is a CLOSING candidate when
  # it uses the same char, is at least as long, and is inside a fence. CommonMark
  # only closes on a bare fence; with trailing text it is reported here.
  if ! awk -v file="$f" '
    {
      line = $0
      sub(/^ {0,3}/, "", line)
      if (match(line, /^(`{3,}|~{3,})/)) {
        run = substr(line, 1, RLENGTH); info = substr(line, RLENGTH + 1)
        gsub(/[ \t]+$/, "", info)
        ch = substr(run, 1, 1); n = length(run)
        if (open == "") { open = ch; olen = n; oline = NR; next }
        if (ch == open && n >= olen) {
          if (info == "") { open = ""; next }
          printf "%s:%d: fence closes with info string \"%s\" (opened at line %d) — use a bare %s\n", file, NR, info, oline, run
          bad = 1; open = ""
        }
      }
    }
    END { exit bad }
  ' "$f"; then
    bad=1
  fi
done

if [[ "$bad" -ne 0 ]]; then
  echo "check-md-fences: FAIL — fix the fences above (a closing fence carries no language)." >&2
  exit 1
fi
echo "check-md-fences: ok — ${#files[@]} file(s), every code fence closes cleanly."
