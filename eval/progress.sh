#!/usr/bin/env bash
# eval/progress.sh — a readable view of the optimiser's progress.
#
# Reads eval/history/optimiser.jsonl and shows the recall curve over accepted
# iterations, the accept/reject tally, and the current learned-rule count.
#
# Usage:
#   eval/progress.sh          one-shot summary
#   eval/progress.sh watch    refresh every 15s (Ctrl+C to stop watching)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
HIST="$HERE/history/optimiser.jsonl"
LEARNED="$(cd "$HERE/.." && pwd -P)/rules/learned"

summary() {
    if [ ! -s "$HIST" ]; then echo "no optimiser history yet ($HIST)"; return; fi
    python3 - "$HIST" "$LEARNED" <<'PY'
import json, sys, os, glob
hist, learned = sys.argv[1], sys.argv[2]
rows = []
for line in open(hist):
    line=line.strip()
    if not line: continue
    try: rows.append(json.loads(line))
    except Exception: pass
if not rows:
    print("no records yet"); sys.exit()

acc = [r for r in rows if r["decision"] == "accept"]
rej = [r for r in rows if r["decision"] == "reject"]

print(f"iterations: {len(rows)}   accepted: {len(acc)}   rejected: {len(rej)}")
print(f"learned rules on disk: {len(glob.glob(os.path.join(learned,'*.rule')))}")
print()

# recall curve over accepted steps (recall only rises on accept)
if acc:
    print("recall curve (accepted steps):")
    last = None
    for r in acc:
        rc = r.get("recall", 0.0)
        bar = "#" * int(rc * 40)
        note = r.get("note","")
        # pull holdout recall out of the note if present
        ho = ""
        if "holdout_recall=" in note:
            ho = "  holdout=" + note.split("holdout_recall=")[1].split()[0]
        arrow = "" if last is None else (f"  (+{rc-last:.3f})" if rc>last else "")
        print(f"  iter {r['iter']:>3}  recall {rc:.3f} |{bar:<40}|{ho}{arrow}")
        last = rc
    best = max(r.get("recall",0.0) for r in acc)
    print(f"\nbest recall so far: {best:.3f}")
else:
    print("no accepted improvements yet")

# last 5 decisions
print("\nlast 5 decisions:")
for r in rows[-5:]:
    print(f"  iter {r['iter']:>3}  {r['decision']:>6}  recall={r.get('recall',0):.3f} precision={r.get('precision',0):.3f}  {r.get('note','')[:50]}")
PY
}

case "${1:-once}" in
  watch)
    while :; do
      clear 2>/dev/null || true
      echo "=== keur optimiser progress  ($(date '+%H:%M:%S')) ==="
      echo ""
      summary
      echo ""
      echo "(refreshing every 15s; Ctrl+C to stop)"
      sleep 15
    done
    ;;
  *)
    summary ;;
esac
