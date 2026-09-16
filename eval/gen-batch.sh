#!/usr/bin/env bash
set -euo pipefail

# gen-batch.sh — generate keur-eval cases for ALL categories (positive+negative)
# in the background. Loops categories.yaml, N cases each, logs progress. Every
# case is a DRAFT (KEUR-NOTE flags REVIEW) — human review before commit.
#
# Usage: gen-batch.sh [per-category] [rounds]
#   per-category  cases per category per round (default 5)
#   rounds        how many passes over all categories (default 3)
# Env: MODEL, KEUR_EVAL, GEN (path to gen-cases.sh)

PER="${1:-5}"; ROUNDS="${2:-3}"
KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
GEN="${GEN:-$HOME/git/hub/keur/eval/gen-cases.sh}"
CATS="$KEUR_EVAL/categories.yaml"
STOP="/tmp/stop-genbatch"
rm -f "$STOP"

log(){ echo "[$(date +%H:%M:%S)] $*"; }
command -v yq >/dev/null || { echo "yq required" >&2; exit 1; }

pos=$(yq -r '.categories[].id' "$CATS")
neg=$(yq -r '.negatives[].id' "$CATS")
log "gen-batch start: per=$PER rounds=$ROUNDS  (stop: touch $STOP)"
log "positive cats: $(echo $pos | tr '\n' ' ')"
log "negative cats: $(echo $neg | tr '\n' ' ')"

for r in $(seq 1 "$ROUNDS"); do
  [[ -f "$STOP" ]] && { log "stop-file seen"; break; }
  log "=== round $r/$ROUNDS ==="
  for c in $pos; do
    [[ -f "$STOP" ]] && break
    "$GEN" "$c" "$PER" >/dev/null 2>&1 && log "  + positive/$c x$PER" || log "  ! failed positive/$c"
  done
  for c in $neg; do
    [[ -f "$STOP" ]] && break
    "$GEN" --negative "$c" "$PER" >/dev/null 2>&1 && log "  + negative/$c x$PER" || log "  ! failed negative/$c"
  done
  # rebuild index each round so progress is queryable
  "$HOME/git/hub/keur/eval/build-index.sh" 2>/dev/null | tail -1 | sed 's/^/  /'
done

total=$(find "$KEUR_EVAL/positive" "$KEUR_EVAL/negative" -type f 2>/dev/null | wc -l | tr -d ' ')
log "gen-batch done: $total case files total"
