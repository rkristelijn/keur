#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# scanners/rules.sh — KEUR rule scanner adapter.
#
# Thin wrapper over the native engine (bin/keur-rules, RE2-powered, ~0.1s on the
# full corpus). Emits KEUR Finding JSON lines. Falls back to the shell engine
# (lib/rules.sh) only when the native binary is not built AND KEUR_NO_NATIVE is
# not forcing shell — the shell path is correct but slow, for compiler-less envs.
#
# Usage: scanners/rules.sh <file...>        (scan an explicit file list)
#        scanners/rules.sh --dir <root>     (scan a directory)
set -uo pipefail

KEUR_HOME="${KEUR_HOME:-$(cd "$(dirname "$0")/.." && pwd -P)}"
export KEUR_HOME
NATIVE="$KEUR_HOME/bin/keur-rules"

if [[ "${KEUR_NO_NATIVE:-0}" != "1" ]] && [[ -x "$NATIVE" ]]; then
  if [[ "${1:-}" = "--dir" ]]; then
    exec "$NATIVE" --dir "${2:-.}"
  else
    exec "$NATIVE" --files "$@"
  fi
fi

# ── fallback: shell engine (slow, dependency-free) ──────────────────────────
if [[ -x "$KEUR_HOME/lib/rules.sh" ]]; then
  if [[ "${1:-}" = "--dir" ]]; then
    # shell engine takes an explicit file list; expand the dir (best effort).
    shift
    root="${1:-.}"
    # find source-ish files, skip the usual noise
    while IFS= read -r f; do set -- "$@" "$f"; done < <(find "$root" -type f \
      -not -path '*/.git/*' -not -path '*/node_modules/*' -not -path '*/vendor/*' 2>/dev/null)
    exec "$KEUR_HOME/lib/rules.sh" "$@"
  fi
  exec "$KEUR_HOME/lib/rules.sh" "$@"
fi

echo "keur: no rule engine available (build src/rules with 'make', or keep lib/rules.sh)" >&2
exit 0
