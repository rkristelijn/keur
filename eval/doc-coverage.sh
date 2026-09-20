#!/usr/bin/env bash
# eval/doc-coverage.sh — measure keur's bidirectional traceability (docs <-> code
# <-> tests) from EXPLICIT tags (docs/design/traceability.md). Read-only; --strict
# makes it a gate. Language/framework-agnostic and sub-second: it greps tags, it
# never builds or runs tests.
# @implements ADR-010
#
# Model: concepts are DECLARED with @id (or a rule id: / ADR filename) and
# REFERENCED with a verb (@implements / @see / @external). A token is an ID only
# if it was declared — no form heuristics, no sed tricks.
#
#   0. @id uniqueness : every declared @id occurs once            [duplicate decl]
#   1. doc -> code    : every 'Coverage: code' ADR has >=1 @implements  [orphan]
#   2. code -> doc    : every @implements/@see token that is an ID resolves [dead link]
#   3. engine -> test : every parser engine has a test                [untested trick]
#   4. cmd -> test    : every CLI subcommand has a [feature:] test     [untested cmd]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
ROOT="$(cd "$HERE/.." && pwd -P)"
cd "$ROOT"

STRICT=0; [ "${1:-}" = "--strict" ] && STRICT=1
FAIL=0
green() { printf "\033[32m%s\033[0m" "$1"; }
red()   { printf "\033[31m%s\033[0m" "$1"; }
pct()   { [ "$2" -eq 0 ] && echo 100 || echo $(( $1 * 100 / $2 )); }
row()   { printf '  %-30s %s\n' "$1" "$2"; }
hdr()   { echo; echo "── $1 ──"; }
# fail row + an explicit, copy-pasteable fix line (for a human OR an AI agent).
failrow() { printf '  %-30s %s\n      %s %s\n' "$1" "$(red "$2")" "$(printf '\033[33mFIX:\033[0m')" "$3"; }

# --- DECLARED concepts (the ID namespace) -----------------------------------
# 1) explicit @id <token> anywhere; 2) rule id: fields; 3) ADR filenames.
# A backtick-wrapped `@id x` is prose (an EXAMPLE explaining the convention), not
# a declaration. Grep whole lines with @id, drop lines where it is backtick-
# wrapped, then extract the token. Real declarations are bare.
ID_DECL=$(grep -rhE --exclude=traceability.md '@id[[:space:]]+[A-Za-z0-9._-]+' src/ rules/ docs/ 2>/dev/null \
          | grep -vE '`@id' \
          | grep -oE '@id[[:space:]]+[A-Za-z0-9._-]+' | awk '{print $2}')
RULE_IDS=$(grep -rhoE '^id:[[:space:]]*[A-Za-z0-9._-]+' rules/ 2>/dev/null | grep -oE '[A-Za-z0-9._-]+$')
ADR_IDS=$(ls docs/adr/ 2>/dev/null | grep -oE 'ADR-[0-9]+')
DECLARED=$(printf '%s\n%s\n%s\n' "$ID_DECL" "$RULE_IDS" "$ADR_IDS" | grep -v '^$' | sort -u)
is_declared() { echo "$DECLARED" | grep -qx "$1"; }

# --- REFERENCES: first token after a verb (space-separated list allowed) ----
# Only REAL references count. A backtick-wrapped `@implements x` / `@see x` is a
# prose example (docs explaining the convention), not a reference — drop those
# lines first. traceability.md (all examples) is excluded wholesale.
GREP_REF=(grep -rhE --exclude=traceability.md)
SCAN_DIRS="src rules docs eval"   # eval/ also holds tooling that carries the tags
no_examples() { grep -vE '`@(implements|see|external|id|proves)'; }
IMPLEMENTS=$("${GREP_REF[@]}" '@implements[[:space:]]+[A-Za-z0-9 ._-]+' $SCAN_DIRS 2>/dev/null | no_examples \
             | grep -oE '@implements[[:space:]]+[A-Za-z0-9 ._-]+' | sed -E 's/@implements[[:space:]]+//' | tr ' ' '\n' | grep -v '^$' | sort -u)
SEE_TOK=$("${GREP_REF[@]}" '@see[[:space:]]+[A-Za-z0-9._:/-]+' $SCAN_DIRS 2>/dev/null | no_examples \
          | grep -oE '@see[[:space:]]+[A-Za-z0-9._:/-]+' | sed -E 's/@see[[:space:]]+//' | grep -v '^$' | sort -u)
EXTERNAL=$("${GREP_REF[@]}" '@external[[:space:]]+[A-Za-z]+:[A-Za-z0-9._-]+' $SCAN_DIRS 2>/dev/null | no_examples \
           | grep -oE '@external[[:space:]]+[A-Za-z]+:[A-Za-z0-9._-]+' | sed -E 's/@external[[:space:]]+//' | sort -u)

adr_coverage() {  # ADR-NNN<TAB>code|none
    for f in docs/adr/ADR-*.md; do
        [ -f "$f" ] || continue
        local id cov
        id=$(basename "$f" | grep -oE 'ADR-[0-9]+')
        cov=$(grep -ioE '\*\*Coverage\*\*:[[:space:]]*(code|none)' "$f" | grep -oiE '(code|none)$' | tr 'A-Z' 'a-z' | head -1)
        printf '%s\t%s\n' "$id" "${cov:-code}"
    done
}
# Engines = the source of truth: the registry entries in engines.cpp plus the
# file-level engines handled in scan.cpp. So a new engine is measured the moment
# it is registered — no hardcoded list to drift.
ENGINES=$( { grep -oE '\{"[a-z-]+",[[:space:]]*&eng_' src/rules/rule/engines.cpp 2>/dev/null | grep -oE '"[a-z-]+"' | tr -d '"'; \
             grep -oE '@id engine-[a-z-]+' src/rules/rule/scan.cpp 2>/dev/null | sed 's/@id engine-//'; } | sort -u)
FEATURES=$(grep -rhoE '\[feature:[a-z-]+\]' src/ 2>/dev/null | sed -E 's/\[feature:(.*)\]/\1/' | sort -u)
CMDS=""; [ -x bin/keur ] && CMDS=$(bin/keur help 2>/dev/null | sed -nE 's/^  ([a-z]+)([[:space:]]+.*)?$/\1/p' | sort -u)

echo "════════════════════════════════════════════════════════════"
echo "  keur traceability coverage (explicit @id tags, refactor-proof)"
echo "════════════════════════════════════════════════════════════"

# --- 0. @id uniqueness ------------------------------------------------------
hdr "0. @id uniqueness (each declared concept exactly once)"
DUPES=$(printf '%s\n%s\n' "$ID_DECL" "$RULE_IDS" | grep -v '^$' | sort | uniq -d)
if [ -n "$DUPES" ]; then
    for d in $DUPES; do failrow "$d" "✗ declared more than once" \
        "grep -rn '@id $d' src rules docs — keep ONE @id $d, rename or remove the duplicate(s)."; done
    FAIL=1
else
    row "declared concepts" "$(green ✓) $(echo "$DECLARED" | grep -c .) unique"
fi

# --- 1. doc -> code ---------------------------------------------------------
hdr "1. doc -> code   (every 'Coverage: code' ADR has >=1 @implements)"
n=0; hit=0
while IFS=$'\t' read -r id cov; do
    [ -z "$id" ] && continue
    if [ "$cov" = "none" ]; then row "$id" "$(green '✓ no-code (deliberate)')"; continue; fi
    n=$((n+1))
    # An ADR is covered if it is @implemented directly OR any of its sub-@id's is
    # (a large ADR is "in progress" once at least one part is built).
    subs=$(echo "$DECLARED" | grep -E "^${id}\." || true)
    sub_hit=""; for s in $subs; do echo "$IMPLEMENTS" | grep -qx "$s" && sub_hit=1 && break; done
    if echo "$IMPLEMENTS" | grep -qx "$id"; then row "$id" "$(green ✓)"; hit=$((hit+1))
    elif [ -n "$sub_hit" ]; then row "$id" "$(green '✓ (via sub-@id)')"; hit=$((hit+1))
    else failrow "$id" "✗ orphan — no @implements" \
        "Add '@implements $id' in the file that builds it (any comment style). If it is a build/process decision with no code site, mark it '**Coverage**: none' in its ADR. See docs/design/traceability.md."; fi
done < <(adr_coverage)
P=$(pct $hit $n); row "coverage" "$hit/$n ($P%)"; [ "$P" -lt 100 ] && FAIL=1

# --- 1b. sub-concept detail (large ADRs, per-part) --------------------------
SUBS=$(echo "$DECLARED" | grep -E '\.' || true)
if [ -n "$SUBS" ]; then
    hdr "1b. sub-concepts (per-part status of large ADRs)"
    for s in $SUBS; do
        if echo "$IMPLEMENTS" | grep -qx "$s"; then row "$s" "$(green '✓ built')"
        else row "$s" "· roadmap (not built)"; fi
    done
fi

# --- 2. code -> doc ---------------------------------------------------------
# A reference is dead only if it LOOKS like a declared-namespace token but is
# not declared. Paths/URLs/free names in @see are valid and skipped.
hdr "2. code -> doc   (every ID-shaped reference resolves)"
n=0; hit=0
check_ref() {  # a token that should be a declared ID
    local t="$1"
    n=$((n+1))
    if is_declared "$t"; then hit=$((hit+1)); else failrow "$t" "✗ dead link (not declared)" \
        "'$t' is referenced but never declared. Either declare it (add '@id $t' where the concept lives) or fix the typo in the @implements/@see that names it."; fi
}
for t in $IMPLEMENTS; do check_ref "$t"; done
# @see: only validate tokens that match the declared-ID SHAPE and aren't paths/urls
for t in $SEE_TOK; do
    case "$t" in */*|*://*|*.md) continue ;; esac      # path / url / filename → not an ID ref
    echo "$DECLARED" | grep -qE "^${t}$|^ADR-|^KEUR-" >/dev/null 2>&1 || true
    if echo "$t" | grep -qE '^(ADR-[0-9]+|KEUR-[A-Z]+-[0-9]+)$'; then check_ref "$t"; fi
done
P=$(pct $hit $n); row "coverage" "$hit/$n ($P%)"; [ "$P" -lt 100 ] && FAIL=1

# --- @external: recorded, not validated -------------------------------------
if [ -n "$EXTERNAL" ]; then
    hdr "@external (other-repo prior art — recorded, not validated)"
    for x in $EXTERNAL; do row "$x" "$(green '· external')"; done
fi

# --- 3. engine -> test ------------------------------------------------------
# How-independent: a concept is proven if ANY @proves points at it, wherever the
# test lives. The TEST TYPE follows from the location (no extra annotation):
#   *_test.cpp        -> unit/integration (proves a code concept)
#   tests/e2e/*.sh    -> e2e/smoke        (proves a system/ADR behaviour)
#   *.rule            -> regression/self-check
hdr "3. engine -> test (@proves, any test type; type shown from location)"
# For each engine, find the FILE that proves it and classify by path.
prove_type() {  # prove_type <id> -> "unit|e2e|regression|-"
    local f
    f=$(grep -rlE "@proves[[:space:]]+[^\\n]*\\b$1\\b" src/ rules/ tests/ eval/ 2>/dev/null | head -1)
    case "$f" in
        "")            echo "-" ;;
        *_test.cpp)    echo "unit" ;;
        */tests/e2e/*) echo "e2e" ;;
        *.rule)        echo "regression" ;;
        *)             echo "other" ;;
    esac
}
n=0; hit=0
for e in $ENGINES; do
    n=$((n+1))
    t=$(prove_type "engine-$e")
    if [ "$t" != "-" ]; then row "$e" "$(green "✓ proven ($t)")"; hit=$((hit+1))
    else failrow "$e" "✗ no @proves engine-$e" \
        "Add a test that runs a rule with 'engine: $e', assert the finding, and tag it '@proves engine-$e' (any test type — unit/e2e/regression). See docs/design/traceability.md."; fi
done
P=$(pct $hit $n); row "coverage" "$hit/$n ($P%)"; [ "$P" -lt 100 ] && FAIL=1

# --- 3b. engine -> doc ------------------------------------------------------
# A DIFFERENT axis: does the SPEC still describe the engine? (doc <-> code
# consistency, not a runtime test.) Every parser engine should have a ###
# heading in rules-authoring.md.
# A ### heading may document several engines at once (e.g.
# "### `file-absence` / `file-presence`"), so pull EVERY `name` token from any
# heading that starts a section, not just single-engine headings.
DOC_ENGINES=$(grep -oE '^### .*' docs/rules-authoring.md 2>/dev/null | grep -oE '`[a-z-]+`' | tr -d '`' | sort -u)
hdr "3b. engine -> doc  (spec describes each engine — consistency, not a test)"
n=0; hit=0
for e in $ENGINES; do
    n=$((n+1))
    if echo "$DOC_ENGINES" | grep -qx "$e"; then row "$e" "$(green '✓ documented')"; hit=$((hit+1))
    else failrow "$e" "✗ not described in the rule-authoring reference" \
        "Add a section describing what the '$e' engine matches to your rule-authoring doc (a '### \`$e\`' heading). See docs/design/traceability.md."; fi
done
P=$(pct $hit $n); row "coverage" "$hit/$n ($P%)"; [ "$P" -lt 100 ] && FAIL=1

# --- 4. cmd -> test ---------------------------------------------------------
hdr "4. cmd -> test   (every subcommand has a [feature:] test)"
if [ -z "$CMDS" ]; then row "(skipped)" "bin/keur not built"; else
    n=0; hit=0
    for c in $CMDS; do
        n=$((n+1))
        if echo "$FEATURES" | grep -qx "$c"; then row "$c" "$(green ✓)"; hit=$((hit+1))
        else failrow "$c" "✗ no [feature:$c]" \
            "Add a test scenario tagged [feature:$c] for the '$c' subcommand (any test type). See docs/design/traceability.md."; fi
    done
    P=$(pct $hit $n); row "coverage" "$hit/$n ($P%)"; [ "$P" -lt 100 ] && FAIL=1
fi

echo
echo "────────────────────────────────────────────────────────────"
if [ "$STRICT" -eq 1 ] && [ "$FAIL" -eq 1 ]; then echo "  $(red FAIL) — gaps above (--strict)"; exit 1; fi
echo "  report complete$([ "$FAIL" -eq 1 ] && echo ' — gaps above (not gating without --strict)')"
