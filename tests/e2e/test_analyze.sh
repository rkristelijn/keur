#!/usr/bin/env bash
# test_analyze.sh — e2e: keur analyze literals classifies into buckets (ADR-058/060).
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

RULES="$(repo_root)/rules"
FIX="$(repo_root)/tests/fixtures/literals"
export KEUR_RULES_DIRS="$RULES"

echo "==> test_analyze ($BINARY)"

# Full run over the fixture.
out="$("$BINARY" analyze literals "$FIX" 2>/dev/null)"

# IDENTITY: email, long id and url must surface in the identity bucket.
echo "$out" | grep -qE 'alice@example\.com'      && ok "identity: email surfaced"      || fail "email not in identity"
echo "$out" | grep -qE '2650567'                 && ok "identity: long id surfaced"    || fail "long id not in identity"
echo "$out" | grep -qE 'api\.example\.com'       && ok "identity: url surfaced"        || fail "url not in identity"

# MAGIC: PENDING appears 3x → rule of three → magic bucket, with count 3.
echo "$out" | grep -qE '3x +\[WORD\] +PENDING'   && ok "magic: PENDING flagged 3x"     || fail "PENDING not flagged as magic 3x"

# NOISE: a language keyword and a tiny number must NOT appear in the report.
echo "$out" | grep -qE '\btrue\b'                && fail "noise 'true' leaked into report" || ok "noise: keyword 'true' not reported"

# --magic shows only the magic bucket (no IDENTITY header).
mg="$("$BINARY" analyze literals "$FIX" --magic 2>/dev/null)"
echo "$mg" | grep -q "IDENTITY" && fail "--magic leaked IDENTITY bucket" || ok "--magic shows only magic"
echo "$mg" | grep -qE 'PENDING' && ok "--magic still shows PENDING" || fail "--magic dropped PENDING"

# --identity shows only the identity bucket (no MAGIC header).
id="$("$BINARY" analyze literals "$FIX" --identity 2>/dev/null)"
echo "$id" | grep -q "MAGIC" && fail "--identity leaked MAGIC bucket" || ok "--identity shows only identity"

# Always exits 0 (analyze never blocks).
"$BINARY" analyze literals "$FIX" >/dev/null 2>&1
[[ "$?" -eq 0 ]] && ok "analyze exits 0 (non-blocking)" || fail "analyze did not exit 0"

summary
