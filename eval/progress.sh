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
RUN_DIR="$HERE/.run"
PIDFILE="$RUN_DIR/supervisor.pid"
SUP_LOG="$RUN_DIR/supervisor.log"
PHASE_FILE="$RUN_DIR/phase"
TURN_FILE="$RUN_DIR/alt-turn"
LOOP_LOG="$HERE/loop.log"

# ---------------------------------------------------------------------------
# live_status — the "what is it doing right now" view. Answers: is the
# supervisor running, which phase, how long has this phase run, how much budget
# is left, which source is training, the latest measured metric, and the last
# few log lines. This is the runtime layer that the optimiser-history summary
# below does not show.
# ---------------------------------------------------------------------------
hms() { # seconds -> Hh Mm Ss (compact)
    local s=$1 h m
    h=$((s/3600)); m=$(((s%3600)/60)); s=$((s%60))
    [[ "$h" -gt 0 ]] && printf '%dh%02dm%02ds' "$h" "$m" "$s" \
        || { [[ "$m" -gt 0 ]] && printf '%dm%02ds' "$m" "$s" || printf '%ds' "$s"; }
}

pbar() { # frac width -> filled bar
    local frac=$1 width=${2:-24} k
    k=$(awk -v f="$frac" -v w="$width" 'BEGIN{k=int(f*w+0.5); if(k<0)k=0; if(k>w)k=w; print k}')
    printf '%*s' "$k" '' | tr ' ' '#'
    printf '%*s' "$((width-k))" '' | tr ' ' '.'
}

live_status() {
    local running=0 pid=""
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
        running=1; pid="$(cat "$PIDFILE")"
    fi

    echo "┌─ live ──────────────────────────────────────────────────┐"
    if [[ "$running" = "1" ]]; then
        # uptime of the supervisor process. `ps -o etimes` is Linux-only, so
        # derive seconds from the process start time (lstart), which both BSD
        # (macOS) and GNU ps provide.
        local lstart start_epoch up=""
        lstart="$(ps -o lstart= -p "$pid" 2>/dev/null | sed 's/^ *//')"
        if [[ -n "$lstart" ]]; then
            start_epoch="$(date -j -f '%a %b %e %T %Y' "$lstart" +%s 2>/dev/null \
                           || date -d "$lstart" +%s 2>/dev/null || true)"
            [[ -n "$start_epoch" ]] && up="up $(hms "$(( $(date +%s) - start_epoch ))")"
        fi
        printf '│ supervisor  RUNNING  pid %-6s %-26s│\n' "$pid" "$up"
    else
        printf '│ supervisor  %-44s│\n' "not running"
    fi

    # current phase: "<n>/<mins>" from the phase file
    local phase_no="?" budget_min="?"
    if [[ -f "$PHASE_FILE" ]]; then
        IFS='/' read -r phase_no budget_min < "$PHASE_FILE"
    fi

    # phase elapsed: timestamp of the last "PHASE <n>:" line in the supervisor log
    local phase_started_epoch="" now elapsed=0 budget_s remain
    if [[ -f "$SUP_LOG" ]]; then
        local ts
        ts="$(grep -E "PHASE ${phase_no}: " "$SUP_LOG" 2>/dev/null | tail -1 \
              | sed -E 's/^\[([0-9-]+ [0-9:]+)\].*/\1/')"
        if [[ -n "$ts" ]]; then
            # macOS `date -j` vs GNU `date -d`
            phase_started_epoch="$(date -j -f '%Y-%m-%d %H:%M:%S' "$ts" +%s 2>/dev/null \
                                   || date -d "$ts" +%s 2>/dev/null || true)"
        fi
    fi
    now="$(date +%s)"
    if [[ "$running" = "1" ]] && [[ -n "$phase_started_epoch" ]]; then
        elapsed=$(( now - phase_started_epoch ))
        [[ "$elapsed" -lt 0 ]] && elapsed=0
    fi

    if [[ "$budget_min" != "?" ]] && [[ "$running" = "1" ]]; then
        budget_s=$(( budget_min * 60 ))
        remain=$(( budget_s - elapsed )); [[ "$remain" -lt 0 ]] && remain=0
        local frac
        frac="$(awk -v e="$elapsed" -v b="$budget_s" 'BEGIN{print (b>0)? e/b:0}')"
        printf '│ phase %-4s  [%s] %3d%%           │\n' \
            "$phase_no" "$(pbar "$frac" 24)" \
            "$(awk -v f="$frac" 'BEGIN{printf "%d", f*100}')"
        printf '│ elapsed %-10s  left ~%-10s budget %sm      │\n' \
            "$(hms "$elapsed")" "$(hms "$remain")" "$budget_min"
    else
        printf '│ phase %-50s│\n' "${phase_no} (no active budget)"
    fi

    # which source is training now (alternate.sh writes the CURRENT turn)
    local src="?"; [[ -f "$TURN_FILE" ]] && src="$(cat "$TURN_FILE")"
    printf '│ training on  %-44s│\n' "$src"

    # latest measured metric from loop.log (the most recent "iter N: ..." line)
    local last_iter
    last_iter="$(grep -E 'iter [0-9]+:' "$LOOP_LOG" 2>/dev/null | tail -1 \
                 | sed -E 's/^\[[0-9:]+\] //')"
    [[ -n "$last_iter" ]] && printf '│ last measure %-44s│\n' "${last_iter:0:44}"
    echo "└─────────────────────────────────────────────────────────┘"

    # last few supervisor log lines: the raw "what is happening" feed
    if [[ -f "$SUP_LOG" ]]; then
        echo ""
        echo "recent activity (supervisor.log):"
        tail -n 6 "$SUP_LOG" | sed 's/^/  /'
    fi
    echo ""
}

summary() {
    if [[ ! -s "$HIST" ]]; then echo "no optimiser history yet ($HIST)"; return; fi
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
      live_status
      summary
      echo ""
      echo "(refresh 15s · Ctrl+C stops)"
      sleep 15
    done
    ;;
  *)
    live_status
    summary ;;
esac
