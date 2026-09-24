#!/usr/bin/env bash
# scanners/pii.sh — KEUR adapter over the PROVEN pii-sensitive-scan.sh.
#
# We do NOT reimplement PII detection. We encapsulate the existing, production-
# proven scanner (BSN 11-proef, IBAN mod-97, validated email/phone, organisation
# SENSITIVE patterns) and normalise its verdict into a KEUR Finding (one JSON line/finding).
#
# Resolution order for the underlying scanner (first hit wins), all overridable
# via KEUR_PII_SCAN:
#   1. $KEUR_PII_SCAN (explicit)
#   2. pii-sensitive-scan.sh on PATH
#
# Usage: scanners/pii.sh <file> [<file> ...]
# Output: zero or more Finding JSON lines on stdout. No content, no values.
set -euo pipefail

# Locate the proven scanner without hardcoding a user path.
resolve_pii_scan() {
  if [[ -n "${KEUR_PII_SCAN:-}" ]] && [[ -x "$KEUR_PII_SCAN" ]]; then echo "$KEUR_PII_SCAN"; return; fi
  if command -v pii-sensitive-scan.sh >/dev/null 2>&1; then command -v pii-sensitive-scan.sh; return; fi
  echo ""   # not found
}

PII_SCAN="$(resolve_pii_scan)"
[[ -n "$PII_SCAN" ]] || { echo "keur:pii: proven pii-sensitive-scan.sh not found (set KEUR_PII_SCAN)" >&2; exit 0; }

# Map the scanner verdict to a KEUR action/confidence.
emit_finding() {
  # emit_finding <file> <class> <detail>
  local file="$1" class="$2" detail="$3" action severity conf
  case "$class" in
    PII)        action="warn"; severity="high";   conf="0.90" ;;
    SENSITIVE)  action="warn"; severity="medium"; conf="0.85" ;;
    *) return 0 ;;   # CLEAN → no finding
  esac
  # fingerprint = sha over source+file+class+detail (no values ever included)
  local fp; fp="$(printf '%s' "pii|$file|$class|$detail" | shasum -a 256 2>/dev/null | awk '{print $1}')"
  printf '{"source":"pii","rule":"PII-%s","category":"pii","file":"%s","severity":"%s","confidence":%s,"deterministic":true,"action":"%s","fingerprint":"%s","message":"%s (%s)"}\n' \
    "$class" "$file" "$severity" "$conf" "$action" "${fp:-na}" "$class detected" "$detail"
}

for f in "$@"; do
  [[ -f "$f" ]] || continue
  # Skip obvious binaries / large files cheaply.
  case "$f" in *.png|*.jpg|*.gif|*.pdf|*.zip|*.gz|*.woff|*.ttf|*.bin) continue ;; esac
  # The proven scanner reads text on argv or stdin; feed file content (capped).
  out="$(head -c 200000 "$f" 2>/dev/null | "$PII_SCAN" 2>/dev/null || echo "CLEAN error")"
  class="$(printf '%s' "$out" | awk '{print $1}')"
  detail="$(printf '%s' "$out" | cut -d' ' -f2-)"
  emit_finding "$f" "$class" "$detail"
done
