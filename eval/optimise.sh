#!/usr/bin/env bash
# eval/optimise.sh — the learning heart. One iteration:
#   1. measure keur vs ground truth (baseline recall/precision)
#   2. take the dominant FN cluster (what keur misses)
#   3. ask Ollama for the SMALLEST rule change to catch it (a candidate .rule)
#   4. re-measure with the candidate added
#   5. GATE: accept only if the benchmark improves (recall up, precision within
#      tolerance, no regression). The benchmark decides, never the LLM.
#
# Accepted candidates are persisted into rules/learned/. Every iteration appends
# a compact record to eval/history/optimiser.jsonl.
#
# Usage:
#   eval/optimise.sh iterate [N]   run N iterations (default 1)
#   eval/optimise.sh baseline      just print the current measurement
#
# Env: MODEL, OLLAMA_URL, KEUR_MIN_PRECISION (gate floor, default 0.50)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"; export KEUR_HOME
MODEL="${MODEL:-qwen2.5-coder:14b-instruct-q5_K_M}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
MIN_PRECISION="${KEUR_MIN_PRECISION:-0.50}"

LEARNED="$KEUR_HOME/rules/learned"          # accepted rules live here
HIST="$HERE/history/optimiser.jsonl"
BASE_RULES="$KEUR_HOME/rules"               # keur's own rules (always loaded)
mkdir -p "$LEARNED" "$HERE/history"

# measure with a given rules dir -> echoes "recall precision f1" (space-sep)
measure() {
    local dirs="$1"
    KEUR_RULES_DIRS="$dirs" "$HERE/measure.sh" >/dev/null 2>&1
    python3 -c "import json;d=json.load(open('$HERE/corpus/measure.json'));print(d['recall'],d['precision'],d['f1'],d['fn'])"
}

# one missed finding + its category, for the prompt
sample_fn() {
    KEUR_RULES_DIRS="$1" "$HERE/measure.sh" --fn 2>/dev/null \
      | awk '/sample missed/{f=1;next} f&&NF{print;exit}'
}

ollama() {
    local prompt="$1"
    local body
    body=$(python3 -c "import json,sys;print(json.dumps({'model':sys.argv[1],'prompt':sys.argv[2],'stream':False,'keep_alive':'10m','options':{'temperature':0.2,'num_predict':500}}))" "$MODEL" "$prompt")
    curl -s "$OLLAMA_URL/api/generate" -d "$body" \
      | python3 -c "import json,sys;print(json.load(sys.stdin).get('response','').strip())"
}

# build the rule-authoring prompt from a missed finding + an example file
build_prompt() {
    local fn_line="$1"
    local cat; cat=$(echo "$fn_line" | awk '{print $2}')
    cat <<EOF
You are improving a deterministic security rule engine. It loads .rule files
(YAML-ish) and matches regex patterns against source files. A proven tool found
a "$cat" issue that our engine MISSED.

Rule file format (respond with ONLY a rule file, no markdown fences, no prose):
id: LEARNED-$(echo "$cat" | tr a-z A-Z)-NNN
title: short title
category: $cat
severity: warning
engine: pattern
target:
  extensions: .js .php .rb .py .env .yml
patterns:
  - regex: A_SINGLE_RE2_REGEX_HERE
    message: what was found
confidence: 0.7

Constraints:
- Use ONE regex, RE2 syntax (no backreferences, no lookahead).
- Secrets appear as an assignment to a key whose name contains key/token/
  secret/password/apikey, in EITHER style:
      apiKey = "long-value"      (equals, e.g. .py .rb .env)
      apiKey: "long-value"       (colon, e.g. .js .json .yml objects)
  So allow BOTH separators: a regex like
      (?i)(key|token|secret|password|apikey)\s*[:=]\s*["'][^"']{12,}["']
  Adjust as needed but keep it ONE regex and specific enough to avoid ordinary code.
- Output ONLY the rule file content, starting with "id:". No trailing spaces.
EOF
}

iterate_once() {
    local iter="$1"
    # snapshot the currently-accepted learned rules alongside base
    local dirs="$BASE_RULES:$LEARNED"
    read -r b_recall b_prec b_f1 b_fn < <(measure "$dirs")
    echo "[iter $iter] baseline: recall=$b_recall precision=$b_prec f1=$b_f1 fn=$b_fn"
    if [ "$b_fn" = "0" ]; then echo "[iter $iter] no FN left; nothing to learn"; return 1; fi

    local fn; fn="$(sample_fn "$dirs")"
    [ -z "$fn" ] && { echo "[iter $iter] no FN sample"; return 1; }
    echo "[iter $iter] target FN: $fn"

    # ask Ollama for a candidate rule
    local candidate; candidate="$(ollama "$(build_prompt "$fn")")"
    # strip markdown fences and trailing whitespace (Ollama often adds both)
    candidate="$(printf '%s\n' "$candidate" | sed -E '/^```/d; s/[[:space:]]+$//')"
    if ! printf '%s' "$candidate" | grep -q '^id:'; then
        echo "[iter $iter] Ollama did not return a valid rule; reject"
        record "$iter" reject "$b_recall" "$b_prec" "no-rule"
        return 0
    fi

    # write candidate to a temp rules dir and re-measure
    local tmpd; tmpd="$(mktemp -d)"; cp -r "$LEARNED/." "$tmpd/" 2>/dev/null || true
    printf '%s\n' "$candidate" > "$tmpd/candidate.rule"
    read -r c_recall c_prec c_f1 c_fn < <(measure "$BASE_RULES:$tmpd")
    echo "[iter $iter] candidate: recall=$c_recall precision=$c_prec f1=$c_f1 fn=$c_fn"

    # THE GATE (benchmark decides): recall must rise, precision stay >= floor and
    # not collapse, no new FN. Never trust the LLM's own opinion.
    local accept
    accept="$(python3 -c "
b_r,b_p,c_r,c_p = float('$b_recall'),float('$b_prec'),float('$c_recall'),float('$c_prec')
print('yes' if (c_r > b_r and c_p >= min($MIN_PRECISION, b_p) ) else 'no')")"

    if [ "$accept" = "yes" ]; then
        local id; id="$(printf '%s' "$candidate" | awk -F': *' '/^id:/{print $2;exit}')"
        cp "$tmpd/candidate.rule" "$LEARNED/${id:-learned-$iter}.rule"
        echo "[iter $iter] ACCEPT -> rules/learned/${id:-learned-$iter}.rule"
        record "$iter" accept "$c_recall" "$c_prec" "$id"
    else
        echo "[iter $iter] REJECT (gate: recall/precision not improved)"
        record "$iter" reject "$c_recall" "$c_prec" "gate"
    fi
    rm -rf "$tmpd"
}

record() {
    python3 -c "import json,sys,time;print(json.dumps({'iter':int(sys.argv[1]),'ts':time.strftime('%Y-%m-%dT%H:%M:%S'),'decision':sys.argv[2],'recall':float(sys.argv[3]),'precision':float(sys.argv[4]),'note':sys.argv[5]}))" "$@" >> "$HIST"
}

case "${1:-iterate}" in
  baseline)
    read -r r p f fn < <(measure "$BASE_RULES:$LEARNED")
    echo "recall=$r precision=$p f1=$f fn=$fn"
    ;;
  iterate)
    N="${2:-1}"
    for i in $(seq 1 "$N"); do
      iterate_once "$i" || break
    done
    ;;
  *) echo "usage: eval/optimise.sh {iterate [N]|baseline}"; exit 1 ;;
esac
