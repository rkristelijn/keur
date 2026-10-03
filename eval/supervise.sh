#!/usr/bin/env bash
# eval/supervise.sh — babysit the training loop as a background job.
#
# Runs the eval/optimiser loop in escalating time budgets so you can watch it
# grow: 5m, then 25m, then 50m, then 50m, ... Each phase is preceded by a
# preflight resource check (disk + memory); if resources are low the supervisor
# waits and retries instead of pushing the machine over. If a phase crashes
# (non-zero exit) it is restarted with backoff, up to a cap, so a transient
# failure does not end the night.
#
# Usage:
#   eval/supervise.sh start        start in the background (nohup), print PID
#   eval/supervise.sh status       running? which phase? last log lines
#   eval/supervise.sh tail         follow the log
#   eval/supervise.sh stop         graceful stop (STOP file + terminate)
#
# Env:
#   KEUR_PHASES        space-separated minute budgets (default "5 25 50 50 50")
#   KEUR_MAX_RESTARTS  restarts per phase before giving up (default 3)
#   KEUR_LOOP_CMD      the worker command (default: eval/loop.sh run)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
RUN_DIR="$HERE/.run"
PIDFILE="$RUN_DIR/supervisor.pid"
LOG="$RUN_DIR/supervisor.log"
STOP="$RUN_DIR/STOP"
STOP_LOOP="/tmp/stop-keur-eval-loop"   # loop.sh / optimise.sh honour this too
PHASE_FILE="$RUN_DIR/phase"
RUN_START="$RUN_DIR/run-start"         # marks THIS run's boundary for progress.sh
HIST_FILE="$HERE/history/optimiser.jsonl"  # cumulative optimiser history

PHASES="${KEUR_PHASES:-5 25 50 50 50}"
MAX_RESTARTS="${KEUR_MAX_RESTARTS:-3}"
LOOP_CMD="${KEUR_LOOP_CMD:-$HERE/loop.sh run}"

mkdir -p "$RUN_DIR"
log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

# ---------------------------------------------------------------------------
# The supervised run: walk the budget ladder, preflight each phase, restart on
# crash. This function is what gets backgrounded.
# ---------------------------------------------------------------------------
supervise_run() {
    rm -f "$STOP" "$STOP_LOOP"   # clear BOTH flags so a restart begins clean;
                                 # a stale /tmp flag otherwise makes every phase
                                 # abort instantly and busy-spins the loop.
    log "supervisor start: phases=[$PHASES] max_restarts=$MAX_RESTARTS pid=$$"

    # Mark THIS run's boundary so progress.sh can separate "this run" from the
    # cumulative history. Line 1: start epoch. Line 2: iter-count in the history
    # at start (records after this index belong to this run). Robust against
    # date-parsing quirks — progress.sh reads the number, not a formatted time.
    {
        date +%s
        wc -l < "$HIST_FILE" 2>/dev/null | tr -d ' ' || echo 0
    } > "$RUN_START"
    local phase_no=0

    # "loop" mode: run indefinitely in fixed-length phases until STOP is set
    # (for overnight runs — `keur learn` until you stop it). Otherwise walk the
    # explicit minute-budget ladder.
    local phase_list="$PHASES"
    local loop_mode=0 loop_mins="${KEUR_LOOP_MINS:-30}"
    if [[ "$PHASES" = "loop" ]]; then loop_mode=1; fi

    while :; do
      if [[ "$loop_mode" = "1" ]]; then
        [[ -f "$STOP" ]] && { log "stop requested; ending loop-mode run"; break; }
        set -- "$loop_mins"           # one phase of loop_mins, repeat forever
      else
        set -- $phase_list
      fi
    for mins in "$@"; do
        phase_no=$((phase_no+1))
        echo "$phase_no/$mins" > "$PHASE_FILE"

        [[ -f "$STOP" ]] && { log "stop requested; exiting before phase $phase_no"; break; }

        # Preflight: only start a phase when disk + memory are healthy. Wait and
        # retry a few times; if still unhealthy, stop cleanly (do not thrash).
        local waited=0
        until "$HERE/preflight.sh" "$KEUR_HOME" >>"$LOG" 2>&1; do
            [[ -f "$STOP" ]] && { log "stop requested during preflight wait"; break 2; }
            waited=$((waited+1))
            if [[ "$waited" -gt 10 ]]; then
                log "preflight still failing after $waited checks; stopping to protect the machine"
                break 2
            fi
            log "preflight not ready (attempt $waited); sleeping 60s"
            sleep 60
        done

        log "PHASE $phase_no: ${mins}m budget"
        local restarts=0
        while :; do
            [[ -f "$STOP" ]] && { log "stop requested; leaving phase $phase_no"; break; }
            # Run the worker for this phase's budget. Time it so we can detect a
            # phase that "completes" instantly — a symptom of a stale stop-flag
            # or a worker that exits 0 without doing work. Blindly looping on
            # those busy-spins the machine (see: the 800-phase incident).
            local t0 t1 elapsed
            t0=$(date +%s)
            if $LOOP_CMD "$mins" >>"$LOG" 2>&1; then
                t1=$(date +%s); elapsed=$((t1 - t0))
                log "PHASE $phase_no complete (${mins}m budget, ran ${elapsed}s)"
                # A real phase runs for many seconds (>=1 iter + sleeps). A near-
                # instant "complete" means no work happened. In loop-mode this
                # would spin forever, so stop and surface it instead.
                if [[ "$loop_mode" = "1" ]] && [[ "$elapsed" -lt 10 ]]; then
                    log "PHASE $phase_no finished in ${elapsed}s (<10s) with no work; \
suspected stop-flag or instant-exit worker. Stopping to avoid a busy-spin. \
Check for a stale /tmp/stop-keur-eval-loop or $STOP, then restart."
                    break 2
                fi
                break
            fi
            rc=$?
            restarts=$((restarts+1))
            if [[ "$restarts" -gt "$MAX_RESTARTS" ]]; then
                log "PHASE $phase_no exceeded $MAX_RESTARTS restarts (last rc=$rc); stopping"
                break 2
            fi
            local backoff=$((restarts*30))
            log "PHASE $phase_no crashed (rc=$rc); restart $restarts/$MAX_RESTARTS after ${backoff}s"
            # Re-check resources before restarting.
            "$HERE/preflight.sh" "$KEUR_HOME" >>"$LOG" 2>&1 || log "preflight warns; restarting anyway"
            sleep "$backoff"
        done
    done
    # loop-mode repeats the phase set until STOP; explicit ladder runs once.
    [[ "$loop_mode" = "1" ]] || break
    done
    log "supervisor done"
    rm -f "$PIDFILE" "$PHASE_FILE"
}

case "${1:-status}" in
  start)
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
      echo "already running (pid $(cat "$PIDFILE"))"; exit 0
    fi
    # Preflight once up front so `start` fails fast if the machine is not ready.
    if ! "$HERE/preflight.sh" "$KEUR_HOME"; then
      echo "not starting: resources not ready (see above)"; exit 1
    fi
    : > "$LOG"
    # Background the internal __run entry via nohup; it writes its own PID.
    nohup "$HERE/supervise.sh" __run >/dev/null 2>&1 &
    sleep 1
    if [[ -f "$PIDFILE" ]]; then
      echo "supervisor started (pid $(cat "$PIDFILE")). Watch: eval/supervise.sh tail"
    else
      echo "supervisor started. Watch: eval/supervise.sh tail"
    fi
    ;;

  __run)
    # internal: the backgrounded body
    echo $$ > "$PIDFILE"
    supervise_run
    ;;

  status)
    if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
      echo "supervisor: RUNNING (pid $(cat "$PIDFILE"))"
      [[ -f "$PHASE_FILE" ]] && echo "phase: $(cat "$PHASE_FILE") (n/minutes)"
      echo "--- last log ---"; tail -8 "$LOG" 2>/dev/null
    else
      echo "supervisor: not running"
      [[ -f "$LOG" ]] && { echo "--- last log ---"; tail -8 "$LOG"; }
    fi
    ;;

  tail)
    tail -f "$LOG"
    ;;

  stop)
    touch "$STOP"                      # supervisor's own stop flag
    touch "/tmp/stop-keur-eval-loop"   # loop.sh's stop flag
    if [[ -f "$PIDFILE" ]]; then
      pid="$(cat "$PIDFILE")"
      log "stop: signalling supervisor pid $pid"
      kill "$pid" 2>/dev/null || true
    fi
    echo "stop requested (STOP file + signal). Loop will exit at the next check."
    ;;

  *)
    echo "usage: eval/supervise.sh {start|status|tail|stop}"; exit 1 ;;
esac
