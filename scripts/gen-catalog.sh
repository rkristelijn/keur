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

# Rule titles may contain multibyte UTF-8 (em-dashes, backticks-with-accents).
# Under a non-UTF-8 locale (CI's LC_ALL=C) sed/tr split them mid-byte and emit
# broken C++ string literals. Force a UTF-8 locale the host actually has.
if ! locale 2>/dev/null | grep -qiE 'UTF-?8'; then
  if locale -a 2>/dev/null | grep -qiE '^C\.utf-?8$'; then export LC_ALL=C.UTF-8
  else export LC_ALL=en_US.UTF-8; fi
fi

out="${1:?usage: gen-catalog.sh <output.cpp> <rule-dir>...}"
shift
dirs=("$@")

# Collect every .rule path once (sorted, deterministic).
tmp_list="$(mktemp)"; tmp_tsv="$(mktemp)"
trap 'rm -f "$tmp_list" "$tmp_tsv"' EXIT
for dir in "${dirs[@]}"; do
  [[ -d "$dir" ]] || continue
  find "$dir" -type f -name '*.rule'
done | sort > "$tmp_list"

rule_total="$(grep -c . "$tmp_list" || true)"

# ---------------------------------------------------------------------------
# ONE awk pass over all rule files (was a bash loop spawning tr+sed per field
# per rule — tens of thousands of subprocesses, minutes at 1000+ rules). awk
# parses each file, normalizes category->subcategory, derives a language from
# the target extensions, and prints one TSV row per rule. No subprocesses.
# Emits: category \t subcategory \t language \t framework \t id \t title \t status
# ---------------------------------------------------------------------------
awk '
  function trim(s){ sub(/^[ \t\r]+/,"",s); sub(/[ \t\r]+$/,"",s); return s }
  function norm_cat(c,   sub_) {
    if (c=="secret"||c=="secrets")                 { CAT="secrets"; SUB="generic"; return }
    if (c=="sast"||c=="cmdi"||c=="sqli"||c=="cbi") { CAT="security"; SUB=c; return }
    if (c=="iac"||c=="cicd")                       { CAT="security"; SUB=c; return }
    if (c=="security")                             { CAT="security"; SUB="generic"; return }
    if (c=="supply-chain")                         { CAT="supply-chain"; SUB="generic"; return }
    if (c=="quality")                              { CAT="quality"; SUB="generic"; return }
    if (c=="inclusivity")                          { CAT="inclusivity"; SUB="generic"; return }
    if (c=="architecture")                         { CAT="architecture"; SUB="generic"; return }
    if (c==""||c=="(uncategorised)")               { CAT="other"; SUB="generic"; return }
    CAT=c; SUB="generic"
  }
  function lang_of(ext,   l) {
    if (ext==".cpp"||ext==".cc"||ext==".c"||ext==".h"||ext==".hpp") return "cpp"
    if (ext==".ts"||ext==".tsx") return "typescript"
    if (ext==".js"||ext==".jsx") return "javascript"
    if (ext==".py") return "python";   if (ext==".go") return "go"
    if (ext==".rb") return "ruby";     if (ext==".java") return "java"
    if (ext==".kt") return "kotlin";   if (ext==".cs") return "csharp"
    if (ext==".php") return "php";     if (ext==".rs") return "rust"
    if (ext==".sh") return "shell";    if (ext==".tf"||ext==".hcl") return "terraform"
    if (ext==".yaml"||ext==".yml"||ext==".json"||ext==".toml"||ext==".ini"||ext==".conf"||ext==".cfg"||ext==".xml"||ext==".properties"||ext==".env") return "config"
    return "other"
  }
  function derive_language(exts,   n,i,arr,l,langs,cnt,only) {
    if (exts=="") return "any"
    n=split(exts, arr, /[ \t]+/); langs=""; cnt=0
    for (i=1;i<=n;i++){ if(arr[i]=="")continue; l=lang_of(arr[i])
      if (index(" " langs " ", " " l " ")==0){ langs=langs " " l; cnt++; only=l } }
    if (cnt==1 && only!="config" && only!="other") return only
    return "any"
  }
  function flush(   dep) {
    if (id=="") return
    norm_cat(rawcat)
    lang=derive_language(exts)
    dep=(status=="deprecated")?"true":"false"
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", CAT, SUB, lang, "any", id, title, dep
  }
  FNR==1 { flush(); id="";title="";rawcat="";status="active";exts="";in_target=0 }
  {
    line=$0
    if (line ~ /^id:[ \t]/)          { id=trim(substr(line,4)) }
    else if (line ~ /^title:[ \t]/)  { title=trim(substr(line,7)) }
    else if (line ~ /^category:[ \t]/){ rawcat=trim(substr(line,10)) }
    else if (line ~ /^status:[ \t]/) { status=trim(substr(line,8)) }
    else if (line ~ /^target:/)      { in_target=1 }
    else if (line ~ /^patterns:/)    { in_target=0 }
    else if (in_target && line ~ /extensions:/) { sub(/.*extensions:/,"",line); exts=trim(line) }
  }
  END { flush() }
' $(cat "$tmp_list") > "$tmp_tsv"

# Dedup by id (5th field), then sort by category, subcategory, id.
sorted="$(sort -t$'\t' -k1,1 -k2,2 -k5,5 "$tmp_tsv" | awk -F'\t' '!seen[$5]++')"

# Emit the C++ via awk too (one pass; escapes backslash + double-quote).
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
  printf '%s\n' "$sorted" | awk -F'\t' 'NF>=7 {
    for (i=1;i<=NF;i++){ gsub(/\\/,"\\\\",$i); gsub(/"/,"\\\"",$i) }
    printf "        {\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", %s},\n", $5,$1,$2,$3,$4,$6,$7
  }'
  echo '    };'
  echo '    return k;'
  echo '}'
  echo
  echo '}  // namespace keur::manager'
} > "$out"

count="$(printf '%s\n' "$sorted" | grep -c . || true)"
echo "gen-catalog: wrote $count rules (from $rule_total files) to $out" >&2

# Completion check: a partial write is a failure, not a silent half-catalogue.
if [[ "$count" -lt 1 ]]; then
  echo "gen-catalog: ERROR wrote 0 rules — refusing a broken catalogue" >&2
  exit 1
fi
