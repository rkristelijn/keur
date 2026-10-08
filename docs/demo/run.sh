#!/usr/bin/env bash
# run.sh — execute the commands a .tape types, without rendering a GIF.
# @shows ADR-047
#
# A .tape is the Arrange/Act/Assert scenario; this is its test-runner. It pulls
# every `Type "..."` / `Type `...`` line (the keystrokes), runs them in zsh (the
# same shell the tape uses), and prints the output. Use it to verify a demo
# still does what its Assert claims — far faster than rendering + reading a frame.
#
#   ./run.sh <feature>.tape        run one tape's commands, show output
#   ./run.sh --all                 run every *.tape in this dir
#
# It does NOT render; use `vhs <tape>` for the GIF. Comments (# ...) are printed
# as section labels so Arrange/Act/Assert stay visible in the output.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"

run_one() {
    local tape="$1"
    [[ -f "$tape" ]] || { echo "no such tape: $tape" >&2; return 2; }
    echo "══════════════════════════════════════════════════════════"
    echo "  run: $(basename "$tape")"
    echo "══════════════════════════════════════════════════════════"
    # Extract the typed text from each `Type "..."` or `Type `...`` line.
    # sed pulls the content between the first pair of quotes/backticks.
    local script
    script=$(sed -nE 's/^Type "(.*)"$/\1/p; s/^Type `(.*)`$/\1/p' "$tape")
    # Run the whole thing in one zsh so cd/exports/setopt persist between lines,
    # exactly like the recorded session. interactive_comments so the # labels
    # are shells comments, not errors (the same setopt the tape sets).
    zsh -c "setopt interactive_comments; $script" 2>&1
    echo
}

main() {
    if [[ "${1:-}" == "--all" ]]; then
        for t in "$HERE"/*.tape; do
            [[ "$(basename "$t")" == "TEMPLATE.tape" ]] && continue
            run_one "$t"
        done
        return 0
    fi
    [[ $# -eq 1 ]] || { grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
    case "$1" in
        /*) run_one "$1" ;;
        *)  run_one "$HERE/$1" ;;
    esac
}

main "$@"
