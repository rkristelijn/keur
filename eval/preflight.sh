#!/usr/bin/env bash
# eval/preflight.sh — resource gate for the training loop.
#
# Answers one question: "is it safe to start another heavy iteration right now?"
# Checks free disk and free memory against thresholds. Exit 0 = go, 1 = wait/stop.
# Portable: macOS (vm_stat / df -m) and Linux (free / df -m).
#
# Usage:  eval/preflight.sh [path]        # path to check disk on (default: cwd)
# Env:    KEUR_MIN_FREE_DISK_MB (default 5000)
#         KEUR_MIN_FREE_MEM_MB  (default 2000)
set -uo pipefail

PATH_CHECK="${1:-.}"
MIN_DISK_MB="${KEUR_MIN_FREE_DISK_MB:-5000}"
MIN_MEM_MB="${KEUR_MIN_FREE_MEM_MB:-2000}"

free_disk_mb() { df -m "$1" 2>/dev/null | awk 'NR==2{print $4}'; }

free_mem_mb() {
    if command -v free >/dev/null 2>&1; then
        # Linux: available column (MB)
        free -m 2>/dev/null | awk '/^Mem:/{print $7}'
    elif command -v vm_stat >/dev/null 2>&1; then
        # macOS: (free + inactive + speculative) pages * page size
        local psize free inact spec
        psize="$(vm_stat | awk '/page size of/{print $8}')"; psize="${psize:-4096}"
        free="$(vm_stat | awk '/Pages free/{gsub("\\.","",$3);print $3}')"
        inact="$(vm_stat | awk '/Pages inactive/{gsub("\\.","",$3);print $3}')"
        spec="$(vm_stat | awk '/speculative/{gsub("\\.","",$3);print $3}')"
        echo $(( (free + inact + spec) * psize / 1048576 ))
    else
        echo 999999   # unknown platform: do not block on memory
    fi
}

disk="$(free_disk_mb "$PATH_CHECK")"; disk="${disk:-0}"
mem="$(free_mem_mb)"; mem="${mem:-0}"

ok=0
if [[ "$disk" -lt "$MIN_DISK_MB" ]]; then
    echo "preflight: LOW DISK ${disk}MB < ${MIN_DISK_MB}MB" >&2; ok=1
fi
if [[ "$mem" -lt "$MIN_MEM_MB" ]]; then
    echo "preflight: LOW MEM ${mem}MB < ${MIN_MEM_MB}MB" >&2; ok=1
fi

[[ "$ok" -eq 0 ]] && echo "preflight: OK (disk ${disk}MB, mem ${mem}MB)"
exit "$ok"
