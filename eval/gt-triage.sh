#!/usr/bin/env bash
# eval/gt-triage.sh — triage the ground-truth false-negatives with a local LLM.
#
# The proven tools (semgrep/bandit/...) that build the ground truth have their
# OWN false positives. When keur misses one (an FN), it may be because the
# proven tool was wrong, not because keur has a gap. Chasing such an FN teaches
# keur a rule for a non-issue.
#
# This script asks Ollama, per missed finding, "is this a real vulnerability or
# a false positive of the scanning tool?" and appends the verdict to
# eval/corpus/gt-suppressions.jsonl. It NEVER edits groundtruth.jsonl. Verdicts
# are written with reviewed:false so a human can audit them; measure.sh drops
# only entries marked verdict:"false-positive". This is a suppression CANDIDATE
# list, not automatic truth (the LLM is itself fallible).
#
# Usage:
#   eval/gt-triage.sh [--limit N] [--model M] [--category C]
#   eval/gt-triage.sh --stats     summarise the suppression file
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
GT="$HERE/corpus/groundtruth.jsonl"
CACHE="$HERE/corpus/repos"
SUP="$HERE/corpus/gt-suppressions.jsonl"
MODEL="${KEUR_TRIAGE_MODEL:-keur-rule-engineer}"
LIMIT=25
CAT_FILTER=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --limit)    LIMIT="$2"; shift 2 ;;
    --model)    MODEL="$2"; shift 2 ;;
    --category) CAT_FILTER="$2"; shift 2 ;;
    --stats)
      [[ -f "$SUP" ]] || { echo "no suppressions yet: $SUP"; exit 0; }
      python3 - "$SUP" <<'PY'
import json,sys,collections
v=collections.Counter(); c=collections.Counter(); rev=collections.Counter()
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    o=json.loads(l); v[o.get("verdict","?")]+=1; c[o.get("category","?")]+=1
    rev["reviewed" if o.get("reviewed") else "unreviewed"]+=1
print("verdicts:", dict(v)); print("by category:", dict(c)); print("review state:", dict(rev))
PY
      exit 0 ;;
    *) echo "gt-triage: unknown arg '$1'" >&2; exit 2 ;;
  esac
done

command -v ollama >/dev/null 2>&1 || { echo "gt-triage: ollama not installed" >&2; exit 1; }
curl -s http://localhost:11434/api/tags >/dev/null 2>&1 || { echo "gt-triage: ollama not reachable" >&2; exit 1; }

# Get the current FN list (file granularity) as "tool category repo/file" lines.
mapfile -t FNS < <(KEUR_MATCH_GRANULARITY=file "$HERE/measure.sh" --fn 2>/dev/null \
  | awk '/^  [a-z]/{print}' | sed -E 's/^  +//')

touch "$SUP"
# Safety cap: never let triage suppress more than this fraction of all FNs in a
# single run — a runaway "everything is FP" judge must not erase the signal.
MAX_FP_FRACTION="${KEUR_TRIAGE_MAX_FP_FRACTION:-0.25}"
total_fn="${#FNS[@]}"
fp_cap=$(python3 -c "print(int($total_fn * $MAX_FP_FRACTION))" 2>/dev/null || echo 0)
fp_count=0
judged=0
for line in "${FNS[@]}"; do
  [[ $judged -ge $LIMIT ]] && break
  tool="$(echo "$line" | awk '{print $1}')"
  cat="$(echo "$line"  | awk '{print $2}')"
  loc="$(echo "$line"  | awk '{print $3}')"
  [[ -n "$CAT_FILTER" && "$cat" != "$CAT_FILTER" ]] && continue
  repo="${loc%%/*}"; relfile="${loc#*/}"
  # already judged?
  grep -qF "\"file\": \"$relfile\"" "$SUP" 2>/dev/null && grep -qF "\"repo\": \"$repo\"" "$SUP" 2>/dev/null && continue

  # Pull the offending line(s) for context (best-effort; small snippet).
  snippet=""
  fpath="$CACHE/$repo/$relfile"
  [[ -f "$fpath" ]] && snippet="$(head -c 1200 "$fpath" 2>/dev/null)"

  prompt="You are a strict security triage assistant. A scanner ('$tool') flagged a '$cat' issue in file '$relfile'. Assume the scanner is RIGHT unless the file content clearly shows it is a false positive. Answer EXACTLY one word on line 1: FALSEPOSITIVE only if you are confident the scanner is wrong here; otherwise REAL. Line 2: a one-sentence reason.

FILE (truncated):
$snippet"

  resp="$(ollama run --nowordwrap "$MODEL" "$prompt" 2>/dev/null | sed $'s/\x1b\\[[0-9;]*[A-Za-z]//g' | head -4)"
  verdict_word="$(echo "$resp" | head -1 | tr '[:lower:]' '[:upper:]' | grep -oE 'REAL|FALSEPOSITIVE' | head -1)"
  reason="$(echo "$resp" | sed -n '2p' | tr -d '"' | tr -cd '[:print:]' | cut -c1-200)"
  [[ -z "$verdict_word" ]] && verdict_word="REAL"   # fail safe: keep it as a gap
  verdict="real"; [[ "$verdict_word" = "FALSEPOSITIVE" ]] && verdict="false-positive"

  # Safety cap: if we've already suppressed the max fraction, keep the rest as
  # REAL so a runaway judge cannot erase the whole learning signal.
  if [[ "$verdict" = "false-positive" ]]; then
    if [[ "$fp_cap" -gt 0 && "$fp_count" -ge "$fp_cap" ]]; then
      verdict="real"; reason="[capped] max FP fraction reached — kept as gap for human review"
    else
      fp_count=$((fp_count+1))
    fi
  fi

  python3 - "$SUP" "$repo" "$relfile" "$cat" "$tool" "$verdict" "$reason" <<'PY'
import json,sys
sup,repo,relfile,cat,tool,verdict,reason = sys.argv[1:8]
with open(sup,"a") as fh:
    fh.write(json.dumps({
        "repo":repo,"file":relfile,"category":cat,"tool":tool,
        "verdict":verdict,"reason":reason,"reviewed":False
    })+"\n")
PY
  echo "  [$verdict] $tool $cat $repo/$relfile"
  judged=$((judged+1))
done
echo "gt-triage: judged $judged finding(s) -> $SUP (verdict:false-positive entries are dropped by measure.sh; reviewed:false for human audit)"
