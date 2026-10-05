#!/usr/bin/env bash
# import-cpm-rules.sh — migrate the full cpm rule corpus into keur (ADR-026 bulk
# import + ADR-039 ility-alignment). Rules stay data (ADR-009): this copies .rule
# files and ensures each carries source/ilities/maturity/action metadata so the
# difficulty vector can steer them and nothing is active that should not be.
#
# For each cpm rule:
#   - DEDUP: if the id already exists in keur's own corpus, SKIP (keur wins).
#   - copy into rules/<primary-ility>/ with `source: cpm`.
#   - derive `ilities` from category (ADR-039 §1 full mapping, below).
#   - derive `maturity` from severity (cpm has none): error->2, warning->3,
#     info->4; deadly families (secrets/sqli/cmdi/xss/injection) forced to 1.
#   - derive `action`: gate ilities (security/hardening/pii) keep block/warn by
#     severity; advisory ilities (quality/performance/inclusivity/reliability/
#     traceability) -> advisory, so they NEVER block by default (scope.md).
#
# SAFE: dry-run unless --apply. Portable: POSIX tools (macOS + Linux/WSL).
# Usage:
#   scripts/import-cpm-rules.sh                 # dry-run summary
#   scripts/import-cpm-rules.sh --apply
#   CPM_RULES=~/git/hub/cpm/rules scripts/import-cpm-rules.sh --apply
set -euo pipefail

APPLY=0
for arg in "$@"; do
  case "$arg" in
  --apply) APPLY=1 ;;
  *)
    echo "unknown flag: $arg" >&2
    exit 2
    ;;
  esac
done

CPM="${CPM_RULES:-$HOME/git/hub/cpm/rules}"
[[ -d "$CPM" ]] || {
  echo "cpm rules dir not found: $CPM" >&2
  exit 1
}

# category -> ilities (primary first), ISO/IEC 25010 sub-characteristics.
# Mirrors scripts/backfill-ilities.sh (ADR-039 §1).
ilities_for() {
  case "$1" in
  secrets | secret | pii | data-exposure | cbi) echo "confidentiality" ;;
  sqli | cmdi | xss | sast | security | supply-chain) echo "integrity" ;;
  iac | k8s | docker | container | ansible | nginx | compliance) echo "integrity" ;;
  oauth | session | api | transport | graphql) echo "authenticity" ;;
  ai-ml | slop) echo "modifiability confidentiality" ;;
  correctness | reliability) echo "reliability-maturity" ;;
  resilience) echo "fault-tolerance" ;;
  observability | logging) echo "availability" ;;
  test | tests) echo "testability" ;;
  quality | architecture) echo "analysability" ;;
  style | patterns | antipattern | best-practice | deprecated | migration) echo "modifiability" ;;
  build | bundler) echo "modularity" ;;
  performance | perf | animation) echo "time-behaviour" ;;
  a11y | accessibility | inclusivity | forms | ui-frameworks | web) echo "accessibility" ;;
  shell | portability) echo "adaptability" ;;
  docs | project | git) echo "traceability" ;;
  *) echo "analysability" ;; # fallback
  esac
}

# ility -> ISO characteristic (the group). Used for the gate/advisory split and
# the target subdir.
characteristic_of() {
  case "$1" in
  confidentiality | integrity | authenticity) echo "security" ;;
  reliability-maturity | fault-tolerance | availability) echo "reliability" ;;
  analysability | modifiability | testability | modularity) echo "maintainability" ;;
  time-behaviour) echo "performance" ;;
  accessibility) echo "usability" ;;
  adaptability | installability) echo "portability" ;;
  traceability) echo "traceability" ;;
  *) echo "maintainability" ;;
  esac
}

# primary ility -> target subdir under rules/ (via its ISO characteristic).
dir_for_ility() {
  case "$(characteristic_of "$1")" in
  security) echo "security" ;;
  usability) echo "inclusivity" ;;
  *) echo "quality" ;; # reliability/maintainability/perf/portability/traceability
  esac
}

# severity -> maturity. deadly families override to 1.
maturity_for() { # $1=severity $2=category
  case "$2" in
  secrets | secret | sqli | cmdi | xss)
    echo 1
    return
    ;;
  esac
  case "$1" in
  error | critical) echo 2 ;;
  warning | warn) echo 3 ;;
  info | *) echo 4 ;;
  esac
}

# primary ility -> GATE (keeps block/warn by severity) or ADVISORY. Only the
# Security characteristic gates by default (scope.md); everything else is
# advisory until a slider raises it.
action_for() { # $1=primary-ility $2=severity
  case "$(characteristic_of "$1")" in
  security)
    case "$2" in error | critical) echo block ;; *) echo warn ;; esac
    ;;
  *) echo advisory ;;
  esac
}

# Build a set of existing keur rule-ids for dedup.
existing_ids="$(grep -rhoE "^id:[[:space:]]*\S+" rules --include='*.rule' | sed -E 's/^id:[[:space:]]*//' | sort -u)"
id_exists() { printf '%s\n' "$existing_ids" | grep -qxF "$1"; }

total=0 imported=0 skipped_dup=0
declare_counts=""
while IFS= read -r f; do
  total=$((total + 1))
  id="$(grep -E '^id:' "$f" | head -1 | sed -E 's/^id:[[:space:]]*//' | awk '{print $1}')"
  cat_val="$(grep -E '^category:' "$f" | head -1 | sed -E 's/^category:[[:space:]]*//' | awk '{print $1}')"
  sev="$(grep -E '^severity:' "$f" | head -1 | sed -E 's/^severity:[[:space:]]*//' | awk '{print $1}')"
  [[ -z "$id" ]] && continue

  if id_exists "$id"; then
    skipped_dup=$((skipped_dup + 1))
    continue
  fi

  ils="$(ilities_for "${cat_val:-quality}")"
  primary="$(echo "$ils" | awk '{print $1}')"
  mat="$(maturity_for "${sev:-info}" "${cat_val:-quality}")"
  act="$(action_for "$primary" "${sev:-info}")"
  subdir="$(dir_for_ility "$primary")"
  arr="[$(echo "$ils" | sed 's/ /, /g')]"

  imported=$((imported + 1))
  if [[ "$APPLY" -eq 1 ]]; then
    dest="rules/$subdir/$(basename "$f")"
    # Copy, then ensure source/ilities/maturity are present (inserted after id:).
    # action: only add if the rule has none (don't override an explicit action).
    has_action=0
    grep -qE '^action:' "$f" && has_action=1
    awk -v arr="$arr" -v mat="$mat" -v act="$act" -v addact="$((1 - has_action))" '
      { print }
      /^id:/ && !done {
        print "source: cpm"
        print "ilities: " arr
        print "maturity: " mat
        if (addact == "1") print "action: " act
        done = 1
      }
    ' "$f" >"$dest"
  else
    printf '%-14s %-24s mat:%s act:%-8s -> rules/%s/\n' "$id" "$arr" "$mat" "$act" "$subdir"
  fi
done < <(find "$CPM" -name '*.rule' | sort)

echo ""
echo "cpm scanned:     $total"
echo "skipped (dup):   $skipped_dup   (id already in keur — keur wins)"
echo "to import:       $imported"
if [[ "$APPLY" -eq 1 ]]; then echo "APPLIED."; else echo "(dry-run — re-run with --apply)"; fi
