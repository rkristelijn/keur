#!/usr/bin/env bash
# test_portability.sh — keur guards its OWN cross-platform portability.
#
# Runs the keur-internal KEUR-PORT-* rules over src/keur and asserts zero
# findings: no platform #ifdef, OS-only header, POSIX-only libc call, hardcoded
# path separator, or POSIX shell command leaked out of the platform layer
# (src/keur/common/). Also proves the controls actually fire by injecting a
# temporary violation, so a broken rule cannot pass silently.
#
# @see rules/keur-internal/KEUR-PORT-*.rule  and docs/adr/ADR-005.
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[ -n "$BINARY" ] || { echo "FAIL: keur binary not found"; exit 1; }

ROOT="$(repo_root)"
export KEUR_RULES_DIRS="$ROOT/rules/keur-internal"

echo "==> test_portability ($BINARY)"

# 1. The real source tree must be clean (all OS-divergent code lives in common/).
findings="$("$BINARY" scan "$ROOT/src/keur" 2>/dev/null | grep -c 'KEUR-PORT-' || true)"
[ "$findings" -eq 0 ] \
  && ok "src/keur is portability-clean (0 KEUR-PORT-*)" \
  || fail "portability control fired on real source ($findings findings) — OS code leaked out of common/"

# 2. The controls must actually detect violations (guards against dead rules).
probe="$ROOT/src/keur/cmd/portability_probe.cpp"
trap 'rm -f "$probe"' EXIT
cat > "$probe" <<'EOF'
#ifdef __APPLE__
#include <unistd.h>
#endif
#include <string>
void probe() {
  int r = system("mkdir -p /tmp/keur_probe");
  (void)WEXITSTATUS(r);
  system("command -v git");
  std::string s;
  int n = (int)s.size();  // word-size truncation (KEUR-PORT-006)
  (void)n;
}
EOF
fired="$("$BINARY" scan "$ROOT/src/keur/cmd" 2>/dev/null \
  | grep -oE 'KEUR-PORT-00[0-9]' | sort -u | wc -l | tr -d ' ')"
rm -f "$probe"
[ "$fired" -ge 5 ] \
  && ok "portability controls detect injected violations ($fired distinct KEUR-PORT rules fired)" \
  || fail "portability controls did NOT fire on an injected violation ($fired) — a rule may be dead"

summary
