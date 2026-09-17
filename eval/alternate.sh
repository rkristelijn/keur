#!/usr/bin/env bash
# eval/alternate.sh — a KEUR_LOOP_CMD wrapper that ALTERNATES the training
# source between corpus and keureval on every phase invocation.
#
# The supervisor calls its loop command once per phase: `$LOOP_CMD <minutes>`.
# This wrapper keeps a tiny counter in eval/.run/alt-turn; on each call it flips
# corpus <-> keureval and exec's the optimiser for that source. So a run of
# phases trains: corpus, keureval, corpus, keureval, ...  Both sources keep
# their own history (tagged by source), so the monitor still shows two clean
# columns — this just makes ONE run feed BOTH.
#
# Usage (via supervise.sh):
#   KEUR_LOOP_CMD="eval/alternate.sh" eval/supervise.sh start
#
# Env: MODEL passes through to optimise.sh unchanged.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
MINS="${1:-25}"
TURN_FILE="$HERE/.run/alt-turn"
mkdir -p "$HERE/.run"

# read previous turn, flip
prev="$(cat "$TURN_FILE" 2>/dev/null || echo keureval)"
if [ "$prev" = "corpus" ]; then
    src="keureval"
else
    src="corpus"
fi
echo "$src" > "$TURN_FILE"

echo "[alternate] this phase trains on: $src (${MINS}m)"
# iterate count scaled loosely to the minute budget; optimise.sh stops early on
# its own stall/convergence and honours the loop stop-file, so a generous cap is
# fine. ~1 iter every ~20-40s -> minutes*3 is a safe upper bound.
exec env KEUR_EVAL_SOURCE="$src" "$HERE/optimise.sh" iterate "$(( MINS * 3 ))"
