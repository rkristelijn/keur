#!/usr/bin/env bash
# run-e2e.sh — run all keur e2e tests with timing.
# Usage: bash tests/e2e/run-e2e.sh [keur-binary]
# Env:   TEST_WARN_MS=2000  TEST_FAIL_MS=15000
set -o nounset
set -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
BINARY="${1:-$(cd "$HERE/../.." && pwd -P)/bin/keur}"
WARN_MS="${TEST_WARN_MS:-2000}"
FAIL_MS="${TEST_FAIL_MS:-15000}"

if [[ ! -x "$BINARY" ]]; then
  echo "ERROR: keur binary not found at $BINARY — run 'make build' first"
  exit 1
fi

echo "==> keur e2e tests against $BINARY"
echo "    (warn > ${WARN_MS}ms, fail > ${FAIL_MS}ms)"
echo ""

PASS=0; FAIL=0
for t in "$HERE"/test_*.sh; do
  name="$(basename "$t")"
  start=$(date +%s%N 2>/dev/null || echo 0)
  if bash "$t" "$BINARY" >/tmp/keur_e2e_out 2>&1; then
    end=$(date +%s%N 2>/dev/null || echo 0)
    ms=$(((end - start) / 1000000))
    if [[ "$ms" -gt "$FAIL_MS" ]]; then
      printf "  \033[31mx %5dms\033[0m %s (TIMEOUT)\n" "$ms" "$name"; FAIL=$((FAIL+1))
    elif [[ "$ms" -gt "$WARN_MS" ]]; then
      printf "  \033[33m! %5dms\033[0m %s (slow)\n" "$ms" "$name"; PASS=$((PASS+1))
    else
      printf "  \033[32mok %4dms\033[0m %s\n" "$ms" "$name"; PASS=$((PASS+1))
    fi
  else
    end=$(date +%s%N 2>/dev/null || echo 0)
    ms=$(((end - start) / 1000000))
    printf "  \033[31mx %5dms\033[0m %s (FAILED)\n" "$ms" "$name"
    sed 's/^/      /' /tmp/keur_e2e_out | tail -6
    FAIL=$((FAIL+1))
  fi
done
rm -f /tmp/keur_e2e_out

echo ""
echo "  Suites: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
