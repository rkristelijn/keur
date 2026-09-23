#!/usr/bin/env bash
# gen-catalog.sh — generate the compile-time rule catalogue for `keur manager`.
#
# Reads every .rule file under the rule directories and emits a C++ source with
# a static array of CatalogEntry{id, category, subcategory, language, framework,
# description, deprecated}. This is the OVERLAY's source of truth: what this
# build of keur *can* enable/disable. Rule MATCHING still comes from the .rule
# files at runtime (rules stay data, ADR-009); this baked list is only the index
# the manager TUI renders, so the overlay is never empty and moves with version.
#
# The 4 FACETS let the TUI re-order the tree (category-first vs language-first,
# etc.). Every rule gets all four, defaulting to "any" so the tree is complete
# and re-orderable with no gaps:
#   category      normalized top bucket   (security | secrets | supply-chain | …)
#   subcategory   finer bucket            (sast | cmdi | sqli | iac | secrets | generic)
#   language      derived from extensions (cpp | typescript | python | … | any)
#   framework     "any" (reserved for later explicit tagging: angular | react | …)
#
# Lifecycle: `status: deprecated` keeps a rule in the catalogue with a marker
# until its .rule is removed (then it drops out naturally).
#
# Deterministic: sorted by category, subcategory, id → stable, clean diffs.
# Usage: gen-catalog.sh <output.cpp> <rule-dir> [<rule-dir> ...]
# Portable: POSIX tools only (no grep -P, no realpath) — macOS + Linux/WSL.
set -euo pipefail

out="${1:?usage: gen-catalog.sh <output.cpp> <rule-dir>...}"
shift
dirs=("$@")

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

trim() { printf '%s' "$1" | tr -d '\r' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//'; }

# Normalize the raw category into (category, subcategory). Security sub-kinds
# (sast/cmdi/sqli/cbi/iac/cicd) roll up under "security" with themselves as the
# subcategory; the "secret" typo folds into "secrets". Everything else keeps its
# category and gets a "generic" subcategory.
normalize_cat() {  # $1 raw category → prints "category\tsubcategory"
  case "$1" in
    secret|secrets)                     printf 'secrets\tgeneric' ;;
    sast|cmdi|sqli|cbi)                  printf 'security\t%s' "$1" ;;
    iac|cicd)                           printf 'security\t%s' "$1" ;;
    security)                           printf 'security\tgeneric' ;;
    supply-chain)                       printf 'supply-chain\tgeneric' ;;
    quality)                            printf 'quality\tgeneric' ;;
    inclusivity)                        printf 'inclusivity\tgeneric' ;;
    architecture)                       printf 'architecture\tgeneric' ;;
    ""|"(uncategorised)")               printf 'other\tgeneric' ;;
    *)                                  printf '%s\tgeneric' "$1" ;;
  esac
}

# Derive a single language from the target extensions. If the rule targets many
# unrelated languages (typical for secrets/supply-chain rules that scan every
# file type) or none, it is language-agnostic → "any". Only a rule scoped to one
# language family gets that language.
derive_language() {  # $1 = space-separated extensions → prints a language
  local exts="$1" langs="" e l
  [ -n "$exts" ] || { printf 'any'; return; }
  for e in $exts; do
    case "$e" in
      .cpp|.cc|.c|.h|.hpp)  l=cpp ;;
      .ts|.tsx)             l=typescript ;;
      .js|.jsx)             l=javascript ;;
      .py)                  l=python ;;
      .go)                  l=go ;;
      .rb)                  l=ruby ;;
      .java)                l=java ;;
      .kt)                  l=kotlin ;;
      .cs)                  l=csharp ;;
      .php)                 l=php ;;
      .rs)                  l=rust ;;
      .sh)                  l=shell ;;
      .tf|.hcl)             l=terraform ;;
      .yaml|.yml|.json|.toml|.ini|.conf|.cfg|.xml|.properties|.env) l=config ;;
      *)                    l=other ;;
    esac
    case " $langs " in *" $l "*) : ;; *) langs="$langs $l" ;; esac
  done
  langs="$(trim "$langs")"
  # exactly one language family → that language; otherwise agnostic.
  if [ "$(printf '%s\n' $langs | wc -l | tr -d ' ')" = "1" ] && [ "$langs" != "config" ] && [ "$langs" != "other" ]; then
    printf '%s' "$langs"
  else
    printf 'any'
  fi
}

for dir in "${dirs[@]}"; do
  [ -d "$dir" ] || continue
  while IFS= read -r f; do
    id=""; title=""; rawcat=""; status="active"; exts=""; in_target=0
    while IFS= read -r line; do
      case "$line" in
        id:\ *)        id="${line#id: }" ;;
        title:\ *)     title="${line#title: }" ;;
        category:\ *)  rawcat="${line#category: }" ;;
        status:\ *)    status="${line#status: }" ;;
        target:*)      in_target=1 ;;
        patterns:*)    in_target=0 ;;
        *extensions:*) [ "$in_target" = 1 ] && exts="${line#*extensions:}" ;;
      esac
    done < "$f"
    [ -n "$id" ] || continue
    id="$(trim "$id")"; title="$(trim "$title")"; rawcat="$(trim "$rawcat")"; status="$(trim "$status")"; exts="$(trim "$exts")"

    IFS=$'\t' read -r category subcategory <<EOF
$(normalize_cat "$rawcat")
EOF
    language="$(derive_language "$exts")"
    framework="any"

    # category \t subcategory \t language \t framework \t id \t title \t status
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$category" "$subcategory" "$language" "$framework" "$id" "$title" "$status" >> "$tmp"
  done < <(find "$dir" -type f -name '*.rule' | sort)
done

# Dedup by id (5th field), then sort by category, subcategory, id.
sorted="$(sort -t$'\t' -k1,1 -k2,2 -k5,5 "$tmp" | awk -F'\t' '!seen[$5]++')"

cpp_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

{
  echo '// GENERATED by scripts/gen-catalog.sh — DO NOT EDIT.'
  echo '// Compile-time rule catalogue for `keur manager`: 4 facets + id/desc/status.'
  echo '// Regenerated from the .rule files at build time. @implements KEUR-MANAGER-CATALOG'
  echo '#include "manager/catalog.h"'
  echo
  echo 'namespace keur::manager {'
  echo
  echo 'const std::vector<CatalogEntry>& catalog() {'
  echo '    static const std::vector<CatalogEntry> k = {'
  if [ -n "$sorted" ]; then
    while IFS=$'\t' read -r category subcategory language framework id title status; do
      dep="false"; [ "$status" = "deprecated" ] && dep="true"
      printf '        {"%s", "%s", "%s", "%s", "%s", "%s", %s},\n' \
        "$(cpp_escape "$id")" \
        "$(cpp_escape "$category")" \
        "$(cpp_escape "$subcategory")" \
        "$(cpp_escape "$language")" \
        "$(cpp_escape "$framework")" \
        "$(cpp_escape "$title")" \
        "$dep"
    done <<< "$sorted"
  fi
  echo '    };'
  echo '    return k;'
  echo '}'
  echo
  echo '}  // namespace keur::manager'
} > "$out"

count="$(printf '%s\n' "$sorted" | grep -c . || true)"
echo "gen-catalog: wrote $count rules to $out" >&2
