#!/usr/bin/env bash
set -uo pipefail

# gen-from-knowledge.sh — draft fictional keur-eval cases from the knowledge
# cache (eval/extend/knowledge/*.jsonl), with a trace of WHY each is / is not a
# finding. For each selected concept it asks Ollama for:
#   1. a POSITIVE case  — fictional code that SHOULD trip a keur rule
#      (annotated `KEUR-EXPECT: <rule>` + a KEUR-NOTE trace),
#   2. a NEGATIVE look-alike — the safe form that must NOT trip it
#      (annotated `KEUR-EXPECT: none` + `KEUR-EXPECT-NOT: <rule>` + trace).
#
# Output are DRAFTS: they land in keur-eval/review-queue/<source>/<concept>/,
# never in positive/ | negative/. The self-check gate (selfcheck-gate.sh) runs
# keur over them next; only survivors are offered for human promotion.
#
# All generated code must be FICTIONAL and short. Sources are inspiration only.
#
# Usage:
#   gen-from-knowledge.sh [--domain D] [--limit N] [--mock]
#     --domain  only concepts whose domain matches (secrets|cmdi|sqli|...)
#     --limit   max concepts to process (default 5)
#     --mock    do not call Ollama; emit deterministic fixture code (for testing
#               the pipeline end-to-end without a model)
# Env: MODEL, OLLAMA_URL, KEUR_EVAL, KNOWLEDGE (cache path), KEUR_RULES_MAP

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/../.." && pwd -P)"
MODEL="${MODEL:-qwen2.5-coder:14b-instruct-q5_K_M}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
KNOWLEDGE="${KNOWLEDGE:-$HERE/knowledge/cpm.jsonl}"

DOMAIN=""; LIMIT=5; MOCK=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain) DOMAIN="$2"; shift 2;;
    --limit)  LIMIT="$2"; shift 2;;
    --mock)   MOCK=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

[[ -f "$KNOWLEDGE" ]] || { echo "knowledge cache not found: $KNOWLEDGE (run knowledge-cpm.sh)" >&2; exit 1; }
[[ -d "$KEUR_EVAL" ]] || { echo "keur-eval not found: $KEUR_EVAL" >&2; exit 1; }

# Map a concept domain -> (keur rule-id, primary language, extension). Only
# domains keur can actually detect today get a real rule-id; others are recall
# targets with a reserved id, kept out of the gate's "must fire" check.
#   domain | rule | lang | ext | comment-style
rule_for() {
  case "$1" in
    secrets) echo "KEUR-SEC-002 py py #";;
    cmdi)    echo "KEUR-SEC-003 php php //";;
    sqli)    echo "SQLI-CONCAT-001 py py #";;   # reserved (recall)
    ssrf)    echo "SSRF-USERURL-001 py py #";;  # reserved (recall)
    *)       echo "" ;;                          # unsupported domain -> skip
  esac
}

if [[ "$MOCK" -eq 0 ]]; then
  curl -sf "$OLLAMA_URL/api/tags" >/dev/null 2>&1 || {
    echo "Ollama not reachable at $OLLAMA_URL. Use --mock to test the pipeline." >&2
    exit 1
  }
fi

# Call Ollama (or the mock) with a prompt; echo the raw snippet (fences stripped).
generate() {
  local prompt="$1" ext="$2" kind="$3" concept_id="$4"
  if [[ "$MOCK" -eq 1 ]]; then
    mock_snippet "$ext" "$kind"
    return
  fi
  local body raw
  body=$(python3 -c "import json,sys;print(json.dumps({'model':sys.argv[1],'prompt':sys.argv[2],'stream':False,'keep_alive':'30m','options':{'temperature':0.6,'num_predict':350}}))" "$MODEL" "$prompt")
  raw=$(curl -s "$OLLAMA_URL/api/generate" -d "$body" | python3 -c "import json,sys;print(json.load(sys.stdin).get('response','').strip())")
  printf '%s\n' "$raw" | sed -E '/^```/d'
}

# Deterministic fixture snippets for --mock (so the whole pipeline is testable
# without a model). Keyed by ext+kind; fictional and rule-tripping by design.
mock_snippet() {
  local ext="$1" kind="$2"
  case "$ext:$kind" in
    py:positive)  printf 'API_TOKEN = "FICTIONAL_EXAMPLE_TOKEN_0000000000"\n';;
    py:negative)  printf 'import os\nAPI_TOKEN = os.environ["API_TOKEN"]\n';;
    php:positive) printf '<?php\n$out = shell_exec("ping -c 1 " . $_GET[\x27host\x27]);\n';;
    php:negative) printf '<?php\n$out = shell_exec("ping -c 1 example.com");\n';;
    *)            printf '# fictional placeholder\n';;
  esac
}

# Build the positive / negative prompts for a concept.
pos_prompt() {  # title, hint, lang
  cat <<EOF
You write SECURITY TEST FIXTURES. Produce a SHORT, FICTIONAL $3 code snippet
that contains this weakness so a static analyzer would flag it:
  weakness: $1
  hint:     $2
Rules: fictional only (use FICTIONAL/EXAMPLE/0000/example.com/192.0.2.x), no real
secrets or real hosts, keep it under 12 lines, must be syntactically valid $3.
Output ONLY the code, no explanation, no markdown fences.
EOF
}
neg_prompt() {  # title, lang
  cat <<EOF
You write SECURITY TEST FIXTURES. Produce a SHORT, FICTIONAL $2 code snippet
that is the SAFE counterpart of this weakness — a look-alike that must NOT be
flagged (the correct, secure way):
  weakness to AVOID: $1
Rules: fictional only, no real secrets/hosts, under 12 lines, syntactically valid
$2. Output ONLY the code, no explanation, no markdown fences.
EOF
}

comment_open()  { case "$1" in php|js|ts|java|go) echo "//";; *) echo "#";; esac; }

# Select concepts from the cache (optionally by domain), cap at LIMIT.
mapfile -t CONCEPTS < <(
  python3 - "$KNOWLEDGE" "$DOMAIN" "$LIMIT" <<'PY'
import json, sys
path, domain, limit = sys.argv[1], sys.argv[2], int(sys.argv[3])
out = []
for line in open(path):
    line = line.strip()
    if not line: continue
    c = json.loads(line)
    if domain and c.get("domain") != domain: continue
    out.append(c)
# stable, spread across domains: keep input order, cap at limit
for c in out[:limit]:
    print(json.dumps(c, ensure_ascii=False))
PY
)

[[ "${#CONCEPTS[@]}" -gt 0 ]] || { echo "no concepts matched (domain='$DOMAIN')"; exit 0; }

drafted=0 skipped=0
for line in "${CONCEPTS[@]}"; do
  cid=$(printf '%s' "$line"    | python3 -c "import json,sys;print(json.load(sys.stdin)['concept_id'])")
  title=$(printf '%s' "$line"  | python3 -c "import json,sys;print(json.load(sys.stdin)['title'])")
  hint=$(printf '%s' "$line"   | python3 -c "import json,sys;print(json.load(sys.stdin).get('hint',''))")
  domain=$(printf '%s' "$line" | python3 -c "import json,sys;print(json.load(sys.stdin).get('domain',''))")
  cwe=$(printf '%s' "$line"    | python3 -c "import json,sys;print(' '.join(json.load(sys.stdin).get('cwe',[])))")

  map=$(rule_for "$domain")
  if [[ -z "$map" ]]; then skipped=$((skipped+1)); continue; fi
  read -r rule lang ext cstyle <<<"$map"
  tier=recall; case "$rule" in KEUR-*) tier=detectable;; esac

  slug=$(printf '%s' "$cid" | tr ':/' '__' | tr -cd 'A-Za-z0-9_-')
  outdir="$KEUR_EVAL/review-queue/$domain"
  mkdir -p "$outdir"

  # --- positive draft ---
  posbody=$(generate "$(pos_prompt "$title" "$hint" "$lang")" "$ext" positive "$cid")
  posfn="$outdir/${slug}-positive.${ext}"
  {
    echo "$cstyle KEUR-EXPECT: $rule"
    echo "$cstyle KEUR-CATEGORY: $domain"
    echo "$cstyle KEUR-SOURCE: $cid${cwe:+ ($cwe)}"
    echo "$cstyle KEUR-TIER: $tier"
    echo "$cstyle KEUR-NOTE: REVIEW — LLM draft ($([ "$MOCK" -eq 1 ] && echo mock || echo "$MODEL")). Finding rationale:"
    echo "$cstyle KEUR-NOTE:   \"$title\" is a $domain weakness; the snippet embeds it, so"
    echo "$cstyle KEUR-NOTE:   $rule should fire. Fictional data only."
    echo ""
    printf '%s\n' "$posbody"
  } > "$posfn"

  # --- negative look-alike draft ---
  negbody=$(generate "$(neg_prompt "$title" "$lang")" "$ext" negative "$cid")
  negfn="$outdir/${slug}-negative.${ext}"
  {
    echo "$cstyle KEUR-EXPECT: none"
    echo "$cstyle KEUR-EXPECT-NOT: $rule"
    echo "$cstyle KEUR-CATEGORY: $domain"
    echo "$cstyle KEUR-SOURCE: $cid${cwe:+ ($cwe)}"
    echo "$cstyle KEUR-NOTE: REVIEW — LLM draft. Non-finding rationale:"
    echo "$cstyle KEUR-NOTE:   safe counterpart of \"$title\"; the weakness is absent, so"
    echo "$cstyle KEUR-NOTE:   $rule must NOT fire (flagging it would be a false positive)."
    echo ""
    printf '%s\n' "$negbody"
  } > "$negfn"

  echo "drafted $cid -> $(basename "$posfn"), $(basename "$negfn")"
  drafted=$((drafted+1))
done

echo ""
echo "drafted $drafted concept(s), skipped $skipped (unsupported domain)."
echo "Drafts are in $KEUR_EVAL/review-queue/. Next: run selfcheck-gate.sh."
