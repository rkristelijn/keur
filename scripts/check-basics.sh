#!/usr/bin/env bash
# check-basics.sh — lint the ONE code-quality norm that the rule engine cannot
# express: directory size (logical units per source directory).
#
# The sibling norm — "no source file > ~300 net lines" — is now a real keur rule
# (KEUR-QUAL-005, engine: max-lines), so it lives in the binary and runs on every
# scan/commit. It was verified to reproduce this script's old file-size verdict
# exactly (manager.cpp, model.cpp). See rules/quality/KEUR-QUAL-005 and
# docs/rules-authoring.md (`max-lines`).
#
# Directory size stays HERE (shell / `make lint`) on purpose: it is a CROSS-FILE
# AGGREGATION — count the logical units in a directory and group .h/.cpp pairs by
# stem — which a per-file rule engine (one file at a time, no sibling knowledge)
# fundamentally cannot express. It would need a directory/aggregation engine
# (ADR-004 stage 2 / the cross-reference engine, ADR-022). Until that lands, this
# one check is the honest home for it.
#
# Usage: scripts/check-basics.sh
# Env:   KEUR_MAX_DIR_FILES (default 10)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
MAX_FILES="${KEUR_MAX_DIR_FILES:-10}"
fail=0

echo "check-basics: <=${MAX_FILES} logical units/dir (file-size → KEUR-QUAL-005)"

# Logical units per source directory (skip vendor). A .h/.cpp pair with the same
# stem counts as ONE unit — the norm is about logical modules per dir, not raw
# file count (6 command pairs in cmd/ is fine, 30 loose files is not).
while IFS= read -r d; do
  n=$(find "$d" -maxdepth 1 -type f | sed -E 's/\.(h|hpp|cpp|cc|c)$//' | sort -u | wc -l | tr -d ' ')
  if [[ "$n" -gt "$MAX_FILES" ]]; then
    echo "  DIR   $d ($n units) — regroup into subdirs"
    fail=1
  fi
done < <(find "$ROOT/src" -type d ! -path '*/vendor/*')

if [[ "$fail" -eq 0 ]]; then
  echo "check-basics: OK"
else
  echo "check-basics: violations found (see TODO.md)"
fi
exit "$fail"
