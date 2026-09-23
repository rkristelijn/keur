#!/usr/bin/env bash
# eval/loop.sh — run the evaluator repeatedly for a time budget, logging each
# iteration to the metrics time series. This is the MEASUREMENT loop (how do the
# rules perform, over time). The optimiser (Ollama changing rules) is a separate,
# later step that sits on top of this and is gated by the acceptance criteria.
#
# Usage:
#   eval/loop.sh run [MINUTES]     # default 10 min; stop early: touch $STOP
#   eval/loop.sh analyze           # summarise eval/metrics.csv (min/mean/max/trend)
#   eval/loop.sh status            # is a loop running?
set -euo pipefail

KEUR_HOME="$(cd "$(dirname "$0")/.." && pwd -P)"
export METRICS_CSV="$KEUR_HOME/eval/metrics.csv"
STOP="/tmp/stop-keur-eval-loop"
LOG="$KEUR_HOME/eval/loop.log"

case "${1:-run}" in
  run)
    MINUTES="${2:-10}"
    rm -f "$STOP"
    end=$(( $(date +%s) + MINUTES*60 ))
    echo "[$(date +%H:%M:%S)] eval-loop start: budget=${MINUTES}m  stop: touch $STOP" | tee -a "$LOG"
    i=0
    while [[ "$(date +%s)" -lt "$end" ]]; do
      [[ -f "$STOP" ]] && { echo "[$(date +%H:%M:%S)] stop-file seen" | tee -a "$LOG"; break; }
      i=$((i+1))
      line="$("$KEUR_HOME/eval/evaluate.sh" --quiet 2>/dev/null | head -1)"
      echo "[$(date +%H:%M:%S)] iter $i: $line" | tee -a "$LOG"
      # small pause so we don't spin; the corpus is static within a run, so
      # repeated iterations measure stability + let the optimiser (later) mutate
      # rules between iterations.
      sleep 20
    done
    echo "[$(date +%H:%M:%S)] eval-loop done after $i iteration(s)" | tee -a "$LOG"
    ;;
  analyze)
    [[ -f "$METRICS_CSV" ]] || { echo "no metrics yet: $METRICS_CSV"; exit 0; }
    python3 - "$METRICS_CSV" <<'PY'
import csv,sys
rows=list(csv.DictReader(open(sys.argv[1])))
if not rows: print("no rows"); sys.exit()
def col(k): return [float(r[k]) for r in rows]
def stat(k):
    v=col(k); return f"{min(v):.3f} / {sum(v)/len(v):.3f} / {max(v):.3f}"
first, last = rows[0], rows[-1]
print(f"runs: {len(rows)}   ({first['ts']} → {last['ts']})")
print(f"                 min /  mean /  max")
for k in ("precision","recall","f1"):
    print(f"  {k:<10}   {stat(k)}")
# trend: last vs first
for k in ("precision","recall","f1"):
    d=float(last[k])-float(first[k])
    arrow = "↑" if d>0.0005 else ("↓" if d<-0.0005 else "→")
    print(f"  {k} trend: {float(first[k]):.3f} → {float(last[k]):.3f}  {arrow} {d:+.3f}")
print(f"  latest: TP={last['tp']} FP={last['fp']} FN={last['fn']} TN={last['tn']} (n={last['n']})")
PY
    ;;
  status)
    if pgrep -f "eval/loop.sh run" >/dev/null 2>&1; then echo "eval-loop: running"; else echo "eval-loop: not running"; fi
    ;;
  *) echo "usage: eval/loop.sh {run [minutes]|analyze|status}"; exit 1 ;;
esac
