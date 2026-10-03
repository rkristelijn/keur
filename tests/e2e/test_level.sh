#!/usr/bin/env bash
# test_level.sh — e2e: keur level --print renders the DOOM difficulty screen
# deterministically (logo braille + the 5 level names), no keyboard, exit 0.
# @proves KEUR-LEVEL
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found (run make build)"; exit 1; }

echo "==> test_level ($BINARY)"

# Non-interactive render at level 3, no colour. Deterministic, pipeable.
# --bucket forces a fixed logo size (independent of the CI terminal height, which
# would otherwise hide the logo via the short-terminal guard).
out="$("$BINARY" level --print 3 --no-color --bucket md 2>/dev/null)"
rc=$?
[[ "$rc" -eq 0 ]] && ok "level --print exits 0" || fail "level --print exit $rc"

# The logo is braille (U+28xx block). In UTF-8 every braille glyph starts with
# the lead byte 0xE2 followed by 0xA0..0xA3. Checking those raw bytes under
# LC_ALL=C is locale-INDEPENDENT: it works on a bare Ubuntu CI container that
# lacks C.UTF-8/en_US.UTF-8 (where a multibyte [⠀-⣿] range silently fails).
if printf '%s' "$out" | LC_ALL=C grep -qE $'\xe2[\xa0-\xa3]'; then
  ok "output contains braille logo glyphs"
else
  fail "no braille logo glyphs in output"
fi

# All five level names appear (text, not braille).
for name in "I'm Too Young to KEUR" "Hey, Not Too Sure" "KEUR Me Plenty" \
            "Ultra Compliance" "H4x0r c0d3fl3x"; do
  echo "$out" | grep -qF "$name" && ok "level name present: $name" \
                                  || fail "missing level name: $name"
done

# The selected row (3) carries the '>' marker and the description block shows.
echo "$out" | grep -qF "> KEUR Me Plenty" && ok "selection marker on level 3" \
                                          || fail "no '>' marker on level 3"
echo "$out" | grep -qF "Level 3" && ok "description block shows Level 3" \
                                  || fail "no Level 3 description block"

# --no-color really suppressed ANSI escapes.
if printf '%s' "$out" | grep -q $'\033\['; then
  fail "--no-color still emitted ANSI escapes"
else
  ok "--no-color output has no ANSI escapes"
fi

# --bucket forces a deterministic size without a tty.
"$BINARY" level --bucket sm --print 1 --no-color >/dev/null 2>&1 \
  && ok "level --bucket sm exits 0" || fail "level --bucket sm non-zero exit"

summary
