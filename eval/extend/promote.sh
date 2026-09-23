#!/usr/bin/env bash
set -uo pipefail

# promote.sh — the human + Kiro step, OUTSIDE the generation loop.
#
# The generator + self-check gate stop at the review queue. Nothing enters the
# corpus automatically. This tool lets a reviewer:
#   --list                 show gate-passed drafts awaiting review (with trace),
#   --show   <path>        print one draft in full (code + rationale trace),
#   --promote <path>       move a reviewed draft into positive/|negative/|
#                          regression/<category>/ and rebuild index.json.
#
# --promote MOVES the file and regenerates the index, then STOPS. It does NOT
# git-commit: the reviewer inspects `git diff` / `git status` and commits by
# hand. That keeps a human in the loop for every corpus change.
#
# Usage:
#   promote.sh --list
#   promote.sh --show   review-queue/secrets/cpm_ANS-013-positive.py
#   promote.sh --promote review-queue/secrets/cpm_ANS-013-positive.py [--regression]
# Env: KEUR_EVAL, KEUR_BUILD_INDEX

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/../.." && pwd -P)"
KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
BUILDER="${KEUR_BUILD_INDEX:-$KEUR_HOME/eval/build-index.sh}"
PASS_LOG="$KEUR_EVAL/review-queue/.gate-pass.jsonl"

usage() { sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
[[ $# -ge 1 ]] || usage 1

case "$1" in
  --list)
    [[ -f "$PASS_LOG" ]] || { echo "no gate-pass log yet — run selfcheck-gate.sh"; exit 0; }
    echo "Gate-passed drafts awaiting human review:"
    echo ""
    python3 - "$PASS_LOG" "$KEUR_EVAL" <<'PY'
import json, sys, os
log, root = sys.argv[1], sys.argv[2]
for line in open(log):
    line = line.strip()
    if not line: continue
    r = json.loads(line)
    p = r["path"]; cls = "positive" if "positive" in p else "negative"
    src = ""
    ap = os.path.join(root, p)
    if os.path.exists(ap):
        for l in open(ap):
            if "KEUR-SOURCE:" in l: src = l.split("KEUR-SOURCE:")[1].strip().rstrip("-->").strip(); break
    print(f"  [{r['status']:10}] {p}")
    print(f"       class={cls}  fired={r.get('fired') or 'none'}  source={src}")
PY
    echo ""
    echo "Review one with:  promote.sh --show <path>"
    echo "Promote one with: promote.sh --promote <path>"
    ;;

  --show)
    [[ $# -ge 2 ]] || usage 1
    f="$KEUR_EVAL/$2"
    [[ -f "$f" ]] || { echo "not found: $f" >&2; exit 1; }
    echo "── $2 ──────────────────────────────────────────"
    cat "$f"
    echo "────────────────────────────────────────────────"
    ;;

  --promote)
    [[ $# -ge 2 ]] || usage 1
    rel="$2"; src="$KEUR_EVAL/$rel"
    [[ -f "$src" ]] || { echo "not found: $src" >&2; exit 1; }
    # target class: negative if the file is a negative draft, else positive,
    # or regression when --regression is passed.
    cls="positive"; case "$(basename "$src")" in *negative*) cls="negative";; esac
    [[ "${3:-}" = "--regression" ]] && cls="regression"
    # category: the review-queue subdir (review-queue/<category>/file)
    cat="$(basename "$(dirname "$src")")"
    dest_dir="$KEUR_EVAL/$cls/$cat"
    dest="$dest_dir/$(basename "$src")"
    mkdir -p "$dest_dir"

    if [[ -e "$dest" ]]; then
      echo "refusing to overwrite existing case: $cls/$cat/$(basename "$src")" >&2
      exit 1
    fi
    git -C "$KEUR_EVAL" mv "$rel" "$cls/$cat/$(basename "$src")" 2>/dev/null \
      || mv "$src" "$dest"
    echo "promoted -> $cls/$cat/$(basename "$src")"

    # refresh the index so the evaluator sees the new case.
    KEUR_EVAL="$KEUR_EVAL" bash "$BUILDER" >/dev/null && echo "index.json rebuilt"

    echo ""
    echo "NOT committed. Review and commit by hand:"
    echo "  cd $KEUR_EVAL && git diff --staged && git status"
    echo "  git commit -m 'corpus: add $cls/$cat/$(basename "$src")'"
    ;;

  *) usage 1 ;;
esac
