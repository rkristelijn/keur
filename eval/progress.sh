#!/usr/bin/env bash
# eval/progress.sh — compact, chart-forward view of the optimiser's progress.
#
# Reads eval/history/optimiser.jsonl. Renders tables + bar charts (█, matching
# cpm's git-timeline.sh convention) instead of prose. Shows the recall curve,
# a recall sparkline over ALL iterations, the accept/reject tally, precision +
# false-positive trend (the keur-eval signal), and the latest decisions.
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
    line = line.strip()
    if not line:
        continue
    try:
        rows.append(json.loads(line))
    except Exception:
        pass
if not rows:
    print("no records yet"); sys.exit()

acc = [r for r in rows if r["decision"] == "accept"]
rej = [r for r in rows if r["decision"] == "reject"]
n_rules = len(glob.glob(os.path.join(learned, "*.rule")))

def bar(frac, width=32, fill="█", empty="·"):
    frac = max(0.0, min(1.0, frac))
    k = int(round(frac * width))
    return fill * k + empty * (width - k)

SPARK = "▁▂▃▄▅▆▇█"
def spark(vals):
    if not vals:
        return ""
    lo, hi = min(vals), max(vals)
    rng = (hi - lo) or 1.0
    return "".join(SPARK[min(len(SPARK) - 1, int((v - lo) / rng * (len(SPARK) - 1)))] for v in vals)

# --- summary table ----------------------------------------------------------
rate = (len(acc) / len(rows) * 100) if rows else 0.0
recalls_all = [r.get("recall", 0.0) for r in rows]
best = max(recalls_all) if recalls_all else 0.0
cur  = recalls_all[-1] if recalls_all else 0.0
print("┌─ optimiser ─────────────────────────────────────────────┐")
print(f"│ iterations {len(rows):>4}   accept {len(acc):>3}   reject {len(rej):>3}   accept% {rate:4.0f} │")
print(f"│ learned rules on disk {n_rules:>3}                                 │")
print(f"│ recall  cur {cur:.3f}   best {best:.3f}                            │")
print("└─────────────────────────────────────────────────────────┘")

# --- recall sparkline over ALL iterations -----------------------------------
print(f"\nrecall trend (all {len(rows)} iters):  {spark(recalls_all)}  {recalls_all[0]:.2f}→{recalls_all[-1]:.2f}")

# --- recall curve over accepted steps (bar chart) ---------------------------
if acc:
    print("\naccepted steps          recall")
    last = None
    for r in acc:
        rc = r.get("recall", 0.0)
        note = r.get("note", "")
        ho = ""
        if "holdout_recall=" in note:
            ho = " h=" + note.split("holdout_recall=")[1].split()[0]
        delta = "" if last is None else (f" +{rc-last:.3f}" if rc > last else f" {rc-last:.3f}")
        print(f"  i{r['iter']:<4} {bar(rc)} {rc:.3f}{ho}{delta}")
        last = rc
else:
    print("\nno accepted improvements yet")

# --- precision / false-positive trend (the keur-eval signal) ----------------
precs = [r.get("precision", 0.0) for r in rows]
print(f"\nprecision trend:  {spark(precs)}  {precs[0]:.2f}→{precs[-1]:.2f}")
# reject reasons at a glance
reasons = {"overfit": 0, "holdout": 0, "gate": 0, "no-rule": 0, "other": 0}
for r in rej:
    note = r.get("note", "")
    if "overfit" in note:      reasons["overfit"] += 1
    elif "holdout" in note:    reasons["holdout"] += 1
    elif "gate" in note:       reasons["gate"] += 1
    elif "no-rule" in note:    reasons["no-rule"] += 1
    else:                       reasons["other"] += 1
if rej:
    tot = len(rej)
    print("\nreject reasons")
    for k, v in sorted(reasons.items(), key=lambda x: -x[1]):
        if v:
            print(f"  {k:<8} {bar(v/tot, 20)} {v:>3}")

# --- last 5 decisions (compact table) ---------------------------------------
print("\nlast 5 decisions")
print("  iter  dec     rec   prec   note")
for r in rows[-5:]:
    dec = "✓" if r["decision"] == "accept" else "✗"
    print(f"  {r['iter']:>4}  {dec:<3} {r.get('recall',0):.3f} {r.get('precision',0):.3f}  {r.get('note','')[:34]}")
PY
}

case "${1:-once}" in
  watch)
    while :; do
      clear 2>/dev/null || true
      echo "keur optimiser  ·  $(date '+%H:%M:%S')"
      summary
      echo ""
      echo "(refresh 15s · Ctrl+C stops)"
      sleep 15
    done
    ;;
  *)
    summary ;;
esac
