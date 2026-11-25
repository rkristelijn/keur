#!/usr/bin/env bash
# verify-render.sh — render demo tapes and check what the GIF really shows.
# @shows ADR-047
#
#   ./verify-render.sh              render + verify every golden-output demo tape
#   ./verify-render.sh <tape>...    only these tapes
#
# gen-golden.sh checks the tape's commands run in a plain shell (run.sh). That
# cannot see what vhs recorded: a working dir deleted mid-render, or text typed
# into a still-running command. Here vhs renders each tape once with an extra
# `Output <name>.screen.txt`, so the GIF and its screen text come from the SAME
# render. The last frame (what a viewer ends on) is then checked for:
#   1. run errors (no such file, getcwd, command not found, ...);
#   2. a verdict line for every `# @shows <ID>` the tape claims;
#   3. interleaving: a typed `# ...` comment that appears WITHOUT the prompt,
#      i.e. it was typed while the previous command was still printing.
# The GIF is copied back only when its frame passes. Needs vhs (and its ttyd and
# ffmpeg); takes about a minute per tape, so it runs locally, not in CI.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
cd "$HERE"
command -v vhs >/dev/null || { echo "verify-render: vhs not found" >&2; exit 2; }

RUN_ERRORS='no such file or directory|getcwd|cannot access parent|unable to read current working|shell-init|command not found'

is_render_target() {
  case "$1" in TEMPLATE.tape|install-*.tape) return 1 ;; *) return 0 ;; esac
}

# Last frame of a vhs .txt capture: frames END with a line of box-drawing dashes.
last_frame() {
  LC_ALL=C awk '/^\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80/ { prev = cur; cur = ""; next }
                { cur = cur $0 "\n" }
                END { printf "%s", (cur ~ /[^[:space:]]/ ? cur : prev) }' "$1" |
    LC_ALL=C sed 's/ *$//'
}

frame_problems() {
  local tape="$1" frame="$2" id comment
  if grep -qiE "$RUN_ERRORS" <<<"$frame"; then
    echo "run error on screen: $(grep -iE "$RUN_ERRORS" <<<"$frame" | head -1)"
  fi
  for id in $(sed -nE 's/^#[[:space:]]*@shows[[:space:]]+(.*)$/\1/p' "$tape" | head -1); do
    grep -qE "\"rule\":\"$id\"|(BLOCK|warn|WARN)[[:space:]]+$id[[:space:]]" <<<"$frame" ||
      echo "no verdict for @shows $id in the last frame"
  done
  while IFS= read -r comment; do
    [[ -z "$comment" ]] && continue
    if LC_ALL=C grep -F -- "$comment" <<<"$frame" | LC_ALL=C grep -qvF -- "❯ $comment"; then
      echo "typed text landed in command output: \"$comment\" (add a Wait after the command before it)"
    fi
  done < <(sed -nE 's/^Type "(# [^"]{8,})"$/\1/p' "$tape")
}

if [[ $# -gt 0 ]]; then tapes=("$@"); else tapes=(*.tape); fi

rc=0
for tape in "${tapes[@]}"; do
  [[ -f "$tape" ]] || { echo "verify-render: no such tape: $tape" >&2; rc=2; continue; }
  is_render_target "$tape" || { echo "verify-render: skip $tape"; continue; }
  name="${tape%.tape}"
  # A throwaway copy with the repo's shape (docs/demo inside a git repo), so a
  # tape that resolves files via `git rev-parse --show-toplevel` finds them.
  work="$(mktemp -d)"
  mkdir -p "$work/docs/demo"
  cp -R "$HERE/." "$work/docs/demo/"
  git -C "$work" init -q
  wd="$work/docs/demo"
  # Same tape, one extra Output: the screen text of this very render.
  sed -i.bak "s#^Output ${name}.gif\$#Output ${name}.gif\nOutput ${name}.screen.txt#" "$wd/$tape"
  if ! (cd "$wd" && vhs "$tape" >"$wd/vhs.log" 2>&1); then
    echo "verify-render: BAD — $tape: vhs failed: $(tail -1 "$wd/vhs.log")" >&2
    rc=1; rm -rf "$work"; continue
  fi
  frame="$(last_frame "$wd/${name}.screen.txt")"
  problems="$(frame_problems "$tape" "$frame")"
  if [[ -n "$problems" ]]; then
    echo "verify-render: BAD — $tape: ${problems//$'\n'/; } (screen text kept: $wd/${name}.screen.txt)" >&2
    rc=1
  else
    cp "$wd/${name}.gif" "$HERE/${name}.gif"
    echo "verify-render: ok — ${name}.gif rendered and its last frame shows the claim"
    rm -rf "$work"
  fi
done
exit "$rc"
