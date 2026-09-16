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
trap 'rm -f "$tmp" "$ROOT/src/keur/adapters/selfcheck_ns.cpp"' EXIT
printf 'void probe(){ popen("x","r"); }\n' > "$tmp"
probe="$("$BINARY" scan "$ROOT/src/keur" 2>/dev/null | grep -c 'KEUR-ARCH-001' || true)"
rm -f "$tmp"
[ "$probe" -ge 1 ] \
  && ok "control detects an injected raw popen() in cmd/ ($probe finding)" \
  || fail "control did NOT detect an injected violation — rule may be dead"

# 3. Namespace-matches-layer controls (KEUR-ARCH-002..004): source is clean.
ns="$("$BINARY" scan "$ROOT/src/keur" 2>/dev/null | grep -cE 'KEUR-ARCH-00[234]' || true)"
[ "$ns" -eq 0 ] \
  && ok "namespaces match their directory layer (0 KEUR-ARCH-002..004)" \
  || fail "namespace/layer mismatch ($ns findings)"

# 4. The namespace control fires when an adapter file uses the wrong namespace.
nsprobe="$ROOT/src/keur/adapters/selfcheck_ns.cpp"
printf 'namespace keur { void x(){} }\n' > "$nsprobe"
nshit="$("$BINARY" scan "$ROOT/src/keur/adapters" 2>/dev/null | grep -c 'KEUR-ARCH-002' || true)"
rm -f "$nsprobe"
[ "$nshit" -ge 1 ] \
  && ok "namespace control detects wrong namespace in adapters/ ($nshit finding)" \
  || fail "namespace control did NOT detect a wrong-namespace adapter file"

summary
