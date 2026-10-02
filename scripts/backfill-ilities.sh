#!/usr/bin/env bash
# backfill-ilities.sh — align every .rule to one or more ilities (ADR-039).
#
# Derives an `ilities:` field from each rule's existing `category:` using the
# ADR-039 §1 seed mapping, and ensures a `maturity:` exists (ADR-026 default 3).
# Rules stay data (ADR-009): this only adds metadata fields, no engine change.
#
# Mapping (ADR-039 §1), primary first:
#   secrets|secret|security|sqli|cmdi|xss|sast  -> security
#   supply-chain                                -> security, hardening
#   iac                                         -> hardening, security
#   quality|architecture                        -> quality
#   data-exposure|cbi                           -> pii, security
#   inclusivity                                 -> inclusivity
#   (unknown)                                   -> quality   (+ warns, so it is visible)
#
# IDEMPOTENT: a rule that already has `ilities:` is left untouched.
# SAFE BY DEFAULT: dry-run unless --apply is passed.
# Portable: POSIX tools only (no grep -P, no realpath) — macOS + Linux/WSL.
#
# Usage:
#   scripts/backfill-ilities.sh            # dry-run: show what WOULD change
#   scripts/backfill-ilities.sh --apply    # write the changes
#   scripts/backfill-ilities.sh --apply rules/security   # limit to a subtree
set -euo pipefail

APPLY=0
ROOTS=()
for arg in "$@"; do
  case "$arg" in
    --apply) APPLY=1 ;;
    --force) FORCE=1 ;;
    -*) echo "unknown flag: $arg" >&2; exit 2 ;;
    *) ROOTS+=("$arg") ;;
  esac
done
[ ${#ROOTS[@]} -eq 0 ] && ROOTS=("rules")
: "${FORCE:=0}"

# category -> space-separated ilities (primary first), ISO/IEC 25010
# sub-characteristics (ADR-039 §1). Covers keur's own + the full cpm corpus.
ilities_for() {
  case "$1" in
    # --- Security ---
    secrets|secret|pii|data-exposure|cbi)       echo "confidentiality" ;;
    sqli|cmdi|xss|sast|security|supply-chain)   echo "integrity" ;;
    iac|k8s|docker|container|ansible|nginx|compliance) echo "integrity" ;;
    oauth|session|api|transport|graphql)        echo "authenticity" ;;
    ai-ml|slop)                                 echo "modifiability confidentiality" ;;
    # --- Reliability ---
    correctness|reliability)                    echo "reliability-maturity" ;;
    resilience)                                 echo "fault-tolerance" ;;
    observability|logging)                      echo "availability" ;;
    test|tests)                                 echo "testability" ;;
    # --- Maintainability ---
    quality|architecture)                       echo "analysability" ;;
    style|patterns|antipattern|best-practice|deprecated|migration) echo "modifiability" ;;
    build|bundler)                              echo "modularity" ;;
    # --- Performance Efficiency ---
    performance|perf|animation)                 echo "time-behaviour" ;;
    # --- Usability ---
    a11y|accessibility|inclusivity|forms|ui-frameworks|web) echo "accessibility" ;;
    # --- Portability ---
    shell|portability)                          echo "adaptability" ;;
    # --- Traceability (process, ADR-010) ---
    docs|project|git)                           echo "traceability" ;;
    *)                                          echo "" ;;  # unknown -> flagged
  esac
}

# id-prefix override: some rules sit in a generic category but are really about a
# specific ility. KEUR-PORT-* are portability rules filed under category:
# architecture — route them to adaptability regardless of category.
ility_override_for_id() {
  case "$1" in
    KEUR-PORT-*) echo "adaptability" ;;
    *)           echo "" ;;
  esac
}

# Format a space-separated list as a .rule inline array: [a, b].
as_array() {
  local out="" i
  for i in $1; do
    [ -n "$out" ] && out="$out, "
    out="$out$i"
  done
  printf '[%s]' "$out"
}

total=0 changed=0 skipped=0 unknown=0 mat_added=0
while IFS= read -r f; do
  total=$((total + 1))

  has_ilities=0
  grep -qE '^ilities:' "$f" && has_ilities=1
  if [ "$has_ilities" -eq 1 ] && [ "$FORCE" -eq 0 ]; then
    skipped=$((skipped + 1))
    continue
  fi

  rid="$(grep -E '^id:' "$f" | head -1 | sed -E 's/^id:[[:space:]]*//' | awk '{print $1}')"
  cat_val="$(grep -E '^category:' "$f" | head -1 | sed -E 's/^category:[[:space:]]*//' | tr -d '\r' | awk '{print $1}')"
  # id-prefix override wins over category (e.g. KEUR-PORT-* -> adaptability).
  ils="$(ility_override_for_id "${rid:-}")"
  [ -z "$ils" ] && ils="$(ilities_for "${cat_val:-}")"
  if [ -z "$ils" ]; then
    unknown=$((unknown + 1))
    echo "UNKNOWN category '${cat_val:-<none>}' -> defaulting to analysability: $f" >&2
    ils="analysability"
  fi
  arr="$(as_array "$ils")"

  need_mat=0
  grep -qE '^maturity:' "$f" || need_mat=1

  changed=$((changed + 1))
  if [ "$APPLY" -eq 1 ]; then
    tmp="$(mktemp)"
    # On --force, drop any existing ilities: line first, then re-insert after id:.
    awk -v arr="$arr" -v addmat="$need_mat" -v force="$FORCE" '
      /^ilities:/ && force == "1" { next }   # strip old ilities when forcing
      { print }
      /^id:/ && !done {
        print "ilities: " arr
        if (addmat == "1") print "maturity: 3"
        done = 1
      }
    ' "$f" > "$tmp"
    mv "$tmp" "$f"
    [ "$need_mat" -eq 1 ] && mat_added=$((mat_added + 1))
  else
    printf 'WOULD SET ilities: %-28s' "$arr"
    [ "$need_mat" -eq 1 ] && printf ' + maturity: 3'
    printf '  <- %s\n' "$f"
  fi
done < <(find "${ROOTS[@]}" -name '*.rule' | sort)

echo ""
echo "scanned:        $total"
echo "already-had:    $skipped (unchanged — use --force to rewrite)"
echo "to-change:      $changed"
echo "unknown-cat:    $unknown (defaulted to analysability — review these)"
if [ "$APPLY" -eq 1 ]; then
  echo "maturity-added: $mat_added"
  echo "APPLIED."
else
  echo "(dry-run — re-run with --apply to write)"
fi
