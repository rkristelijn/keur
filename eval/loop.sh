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
    # Early-stop on stagnation: this is a MEASUREMENT loop over a STATIC corpus,
    # so once the metrics stop moving there is nothing more to observe — without
    # an optimiser mutating rules between iterations, every further iteration
    # measures the identical result. Repeating that for a whole time budget just
    # burns CPU (observed: 45 min of identical iters). If the metric line is
    # unchanged for KEUR_LOOP_MAX_STALL iterations (default 3), stop and say why.
    # Set KEUR_LOOP_MAX_STALL=0 to disable (e.g. when an optimiser IS mutating
    # rules between iterations and you want to keep measuring).
    max_stall="${KEUR_LOOP_MAX_STALL:-3}"
    i=0; stall=0; prev=""
    while [[ "$(date +%s)" -lt "$end" ]]; do
      [[ -f "$STOP" ]] && { echo "[$(date +%H:%M:%S)] stop-file seen" | tee -a "$LOG"; break; }
      i=$((i+1))
      line="$("$KEUR_HOME/eval/evaluate.sh" --quiet 2>/dev/null | head -1)"
      echo "[$(date +%H:%M:%S)] iter $i: $line" | tee -a "$LOG"

      # FN=0 means keur already reproduces every authored positive — no recall
      # left to gain; the only headroom is precision (the FP count). Surface it.
      if echo "$line" | grep -q 'FN=0 '; then
        echo "[$(date +%H:%M:%S)] note: FN=0 — full recall on this corpus; no recall to learn. Remaining headroom is precision (reduce FP). A recall-driven optimiser has nothing to do here." | tee -a "$LOG"
      fi

      # Stagnation check: compare the metric payload (ignoring the timestamp).
      if [[ "$max_stall" -gt 0 ]]; then
        if [[ "$line" == "$prev" ]]; then
          stall=$((stall+1))
        else
          stall=0
        fi
        prev="$line"
        if [[ "$stall" -ge "$max_stall" ]]; then
          echo "[$(date +%H:%M:%S)] early-stop: metrics unchanged for $max_stall iterations (static corpus, no optimiser mutating rules). Nothing more to measure — stopping instead of busy-spinning. Set KEUR_LOOP_MAX_STALL=0 to keep measuring." | tee -a "$LOG"
          break
        fi
      fi

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
