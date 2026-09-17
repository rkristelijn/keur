#!/usr/bin/env bash
# check-basics.sh — lint keur's OWN code-quality basics (not a functional test).
#
# Enforces the norms agreed for this codebase:
#   1. no source file > MAX_LINES net lines (comments/blank excluded);
#   2. no source directory > MAX_FILES files.
#
# This is a LINT/quality check over keur's source (counting lines/files), NOT an
# end-to-end behavioural test — hence it lives in scripts/ and hangs off `make
# lint`, not the e2e suite. These are AGGREGATION checks the current line/pattern
# rule engine cannot express; that needs the file/aggregation engine adopted
# from cpm (ADR-004 stage 2). When that lands, port these to real .rule files.
#
# Usage: scripts/check-basics.sh
# Env:   KEUR_MAX_FILE_LINES (default 300), KEUR_MAX_DIR_FILES (default 10)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
MAX_LINES="${KEUR_MAX_FILE_LINES:-300}"
MAX_FILES="${KEUR_MAX_DIR_FILES:-10}"
fail=0

echo "check-basics: <=${MAX_LINES} net lines/file, <=${MAX_FILES} files/dir"

# 1. net lines per C++ source (exclude comments/blank; skip vendor + test files)
while IFS= read -r f; do
  net=$(grep -vcE '^[[:space:]]*(//|/\*|\*|$)' "$f")
  if [ "$net" -gt "$MAX_LINES" ]; then
    echo "  FILE  $f ($net net lines) — split by responsibility"
    fail=1
  fi
done < <(find "$ROOT/src" \( -name '*.cpp' -o -name '*.h' \) \
           ! -path '*/vendor/*' ! -name '*_test.cpp')

# 2. files per source directory (skip vendor)
while IFS= read -r d; do
  n=$(find "$d" -maxdepth 1 -type f | wc -l | tr -d ' ')
  if [ "$n" -gt "$MAX_FILES" ]; then
    echo "  DIR   $d ($n files) — regroup into subdirs"
    fail=1
  fi
done < <(find "$ROOT/src" -type d ! -path '*/vendor/*')

if [ "$fail" -eq 0 ]; then
  echo "check-basics: OK"
else
  echo "check-basics: violations found (see TODO.md)"
fi
exit "$fail"
