#!/usr/bin/env bash
# test_selfcheck.sh — keur guards its OWN architecture (dogfooding).
#
# Runs the keur-internal rules over src/keur and asserts zero findings, i.e.
# no raw I/O (popen/system/mkdir/unlink) leaked out of the adapters layer.
# Also proves the control actually fires by injecting a temporary violation.
#
# @see rules/keur-internal/KEUR-ARCH-001-io-via-adapter.rule  and ADR-003.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

ROOT="$(repo_root)"
export KEUR_RULES_DIRS="$ROOT/rules/keur-internal"

echo "==> test_selfcheck ($BINARY)"

# 1. The real source tree must be clean (all I/O lives in adapters/).
findings="$("$BINARY" scan "$ROOT/src/keur" 2>/dev/null | grep -c 'KEUR-ARCH-001' || true)"
[ "$findings" -eq 0 ] \
  && ok "src/keur has no raw I/O outside adapters (0 KEUR-ARCH-001)" \
  || fail "architecture control fired on real source ($findings findings) — I/O leaked out of adapters/"

# 2. The control must actually detect a violation (guards against a dead rule).
tmp="$ROOT/src/keur/cmd/selfcheck_probe.cpp"
trap 'rm -f "$tmp"' EXIT
printf 'void probe(){ popen("x","r"); }\n' > "$tmp"
probe="$("$BINARY" scan "$ROOT/src/keur" 2>/dev/null | grep -c 'KEUR-ARCH-001' || true)"
rm -f "$tmp"
[ "$probe" -ge 1 ] \
  && ok "control detects an injected raw popen() in cmd/ ($probe finding)" \
  || fail "control did NOT detect an injected violation — rule may be dead"

summary
