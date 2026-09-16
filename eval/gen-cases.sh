#!/usr/bin/env bash
set -euo pipefail

# gen-cases.sh — draft FICTIONAL keur-eval cases with a local Ollama model.
# Reads categories.yaml (in keur-eval), asks Ollama to produce a fictional
# snippet per the category prompt, prepends the KEUR-EXPECT annotation, and
# writes it under keur-eval/<class>/<category>/. Human review before commit.
#
# Usage: gen-cases.sh <category-id> [count]   (default count 3)
#        gen-cases.sh --negative <neg-id> [count]
#
# Env: MODEL (default qwen2.5-coder:14b-instruct-q5_K_M), KEUR_EVAL (repo path),
#      OLLAMA_URL (default http://localhost:11434)

MODEL="${MODEL:-qwen2.5-coder:14b-instruct-q5_K_M}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
CATS="$KEUR_EVAL/categories.yaml"

die(){ echo "ERROR: $*" >&2; exit 1; }
command -v yq >/dev/null || die "yq required (brew install yq)"
command -v curl >/dev/null || die "curl required"
[[ -f "$CATS" ]] || die "categories.yaml not found: $CATS"
curl -sf "$OLLAMA_URL/api/tags" >/dev/null || die "Ollama not running"

NEG=0; [[ "${1:-}" == "--negative" ]] && { NEG=1; shift; }
CID="${1:?category id required}"; COUNT="${2:-3}"
SEL=$([ "$NEG" -eq 1 ] && echo ".negatives[] | select(.id==\"$CID\")" || echo ".categories[] | select(.id==\"$CID\")")

domain=$(yq -r "$SEL | .domain" "$CATS")
prompt=$(yq -r "$SEL | .prompt" "$CATS")
langs=$(yq -r "$SEL | .langs | join(\" \")" "$CATS")
[[ "$domain" != "null" && -n "$domain" ]] || die "category '$CID' not found"

if [[ "$NEG" -eq 1 ]]; then
  cls="negative"; expect_not=$(yq -r "$SEL | .expect_not | join(\" \")" "$CATS"); rules=""
else
  cls="positive"; rules=$(yq -r "$SEL | .rules | join(\" \")" "$CATS"); expect_not=""
fi
OUTDIR="$KEUR_EVAL/$cls/$CID"; mkdir -p "$OUTDIR"

gen() {
  local body
  body=$(python3 -c "import json,sys;print(json.dumps({'model':sys.argv[1],'prompt':sys.argv[2],'stream':False,'keep_alive':'30m','options':{'temperature':0.7,'num_predict':400}}))" "$MODEL" "$1")
  curl -s "$OLLAMA_URL/api/generate" -d "$body" | python3 -c "import json,sys;print(json.load(sys.stdin).get('response','').strip())"
}

# pick first lang for extension
ext=$(echo "$langs" | awk '{print $1}')
comment_for(){ case "$1" in py|sh|env|yaml|yml) echo '#';; js|java) echo '//';; md|txt) echo '<!--';; *) echo '#';; esac; }
cend_for(){ case "$1" in md|txt) echo ' -->';; *) echo '';; esac; }

for i in $(seq 1 "$COUNT"); do
  raw=$(gen "$prompt

Output ONLY the snippet, no explanation, no markdown fences. Language: $ext. Keep it fictional and short.")
  # strip accidental code fences
  raw=$(printf '%s' "$raw" | sed -E '/^```/d')
  c=$(comment_for "$ext"); ce=$(cend_for "$ext")
  ts=$(date +%s); fn="$OUTDIR/${CID}-${ts}-${i}.${ext}"
  {
    if [[ "$NEG" -eq 1 ]]; then
      echo "$c KEUR-EXPECT: none$ce"
      [[ -n "$expect_not" ]] && echo "$c KEUR-EXPECT-NOT: $expect_not$ce"
    else
      echo "$c KEUR-EXPECT: $rules$ce"
    fi
    echo "$c KEUR-CATEGORY: $CID$ce"
    echo "$c KEUR-NOTE: LLM-drafted fictional case ($MODEL) — REVIEW before commit$ce"
    echo ""
    printf '%s\n' "$raw"
  } > "$fn"
  echo "wrote $fn"
done
echo "Done. REVIEW these before committing; then run build-index.sh."
