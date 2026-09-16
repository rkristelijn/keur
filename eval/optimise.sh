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
# Prefer the expertise-primed model (eval/Modelfile) when it exists; fall back
# to the base coder model otherwise. Override with MODEL=...
if [ -z "${MODEL:-}" ]; then
    if curl -sf "${OLLAMA_URL:-http://localhost:11434}/api/tags" 2>/dev/null | grep -q '"keur-rule-engineer'; then
        MODEL="keur-rule-engineer"
    else
        MODEL="qwen2.5-coder:14b-instruct-q5_K_M"
    fi
fi
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
MIN_PRECISION="${KEUR_MIN_PRECISION:-0.50}"

LEARNED="$KEUR_HOME/rules/learned"          # accepted rules live here
HIST="$HERE/history/optimiser.jsonl"
BASE_RULES="$KEUR_HOME/rules"               # keur's own rules (always loaded)
mkdir -p "$LEARNED" "$HERE/history"

# Which split the optimiser trains on. Holdout stays unseen during training and
# is only measured for a generalisation report (overfitting check).
TRAIN_SPLIT="${KEUR_TRAIN_SPLIT:-train}"

# measure with a given rules dir -> echoes "recall precision f1 fn" (space-sep)
measure() {
    local dirs="$1"; local split="${2:-$TRAIN_SPLIT}"
    KEUR_SPLIT="$split" KEUR_RULES_DIRS="$dirs" "$HERE/measure.sh" >/dev/null 2>&1
    python3 -c "import json;d=json.load(open('$HERE/corpus/measure.json'));print(d['recall'],d['precision'],d['f1'],d['fn'])"
}

# sample_fn <rules-dir> <index> -> one missed finding, ROTATING through the
# distinct missed clusters so each iteration attacks a different one instead of
# re-proposing rules for the same hard case forever.
sample_fn() {
    local idx="${2:-0}"
    KEUR_SPLIT="$TRAIN_SPLIT" KEUR_RULES_DIRS="$1" "$HERE/measure.sh" --fn 2>/dev/null \
      | awk '/sample missed/{f=1;next} f&&NF{print}' \
      | awk -v n="$idx" 'NR>=1{a[NR]=$0} END{if(NR==0)exit; print a[(n % NR)+1]}'
}

# regex_generalises <candidate-rule-text> -> exit 0 if the regex is a
# generalising matcher, 1 if it looks like a memorised literal (overfit).
# Guards against Ollama pasting the secret value into the pattern.
regex_generalises() {
    printf '%s\n' "$1" | python3 -c '
import sys, re
text = sys.stdin.read()
# extract the regex: line(s)
patterns = re.findall(r"^\s*-\s*regex:\s*(.+?)\s*$", text, re.M)
if not patterns:
    sys.exit(1)
for pat in patterns:
    # 1. Must contain at least one quantifier or character class — i.e. it
    #    matches a FAMILY of strings, not one fixed value.
    has_class = bool(re.search(r"\[[^\]]+\]|\\[dwsDWS]", pat))
    has_quant = bool(re.search(r"[+*]|\{\d+(,\d*)?\}|\?", pat))
    if not (has_class or has_quant):
        sys.exit(1)   # pure literal like "1234" or "AKIA..." -> overfit
    # 2. Reject a long alphanumeric literal run (>=10) embedded in the pattern:
    #    that is almost certainly a copied secret value, not a matcher.
    #    Strip regex metachar groups first so we only see literal runs.
    literal = re.sub(r"\\.|\[[^\]]*\]|\([^)]*\)|[.*+?{}|^$]", "", pat)
    if re.search(r"[A-Za-z0-9]{10,}", literal):
        sys.exit(1)   # long literal run -> memorised value
sys.exit(0)
'
}

ollama() {
    local prompt="$1"
    local body
    body=$(python3 -c "import json,sys;print(json.dumps({'model':sys.argv[1],'prompt':sys.argv[2],'stream':False,'keep_alive':'10m','options':{'temperature':0.2,'num_predict':500}}))" "$MODEL" "$prompt")
    curl -s "$OLLAMA_URL/api/generate" -d "$body" \
      | python3 -c "import json,sys;print(json.load(sys.stdin).get('response','').strip())"
}

# build the rule-authoring prompt from a missed finding + an example file.
# The deep expertise (RE2, .rule format, generalise-don't-memorise) lives in the
# model's SYSTEM prompt (eval/Modelfile -> keur-rule-engineer), so this per-call
# prompt only has to state the concrete task.
build_prompt() {
    local fn_line="$1"
    local cat; cat=$(echo "$fn_line" | awk '{print $2}')
    cat <<EOF
A proven security tool found a "$cat" issue that our rule engine MISSED.
Write ONE keur .rule that catches this class of "$cat" issue and generalises to
other repos (not a memorised literal).

Start the file with:  id: LEARNED-$(echo "$cat" | tr a-z A-Z)-NNN
Use category: $cat, engine: pattern, and target these extensions:
  .js .php .rb .py .env .yml .json .ts .go .java

Output ONLY the rule file, starting with "id:". No markdown fences, no prose.
EOF
}

iterate_once() {
    local iter="$1"
    # snapshot the currently-accepted learned rules alongside base
    local dirs="$BASE_RULES:$LEARNED"
    read -r b_recall b_prec b_f1 b_fn < <(measure "$dirs")
    echo "[iter $iter] baseline: recall=$b_recall precision=$b_prec f1=$b_f1 fn=$b_fn"
    if [ "$b_fn" = "0" ]; then echo "[iter $iter] no FN left; nothing to learn"; return 1; fi

    local fn; fn="$(sample_fn "$dirs" "$iter")"
    [ -z "$fn" ] && { echo "[iter $iter] no FN sample"; return 1; }
    echo "[iter $iter] target FN: $fn"

    # ask Ollama for a candidate rule
    local candidate; candidate="$(ollama "$(build_prompt "$fn")")"
    # Clean the candidate:
    #  - strip markdown fences (```yaml / ```), trailing whitespace
    #  - collapse double-escaped backslashes (\\s -> \s): the model sometimes
    #    over-escapes for JSON; RE2 needs single backslashes in the .rule file.
    #  - strip surrounding quotes the model may wrap around the regex value
    #    (keur's parser wants the regex raw/unquoted).
    candidate="$(printf '%s\n' "$candidate" \
        | sed -E '/^[[:space:]]*```/d; s/[[:space:]]+$//' \
        | sed -E 's/\\\\/\\/g' \
        | sed -E "s/^([[:space:]]*- regex: )'(.*)'$/\1\2/; s/^([[:space:]]*- regex: )\"(.*)\"$/\1\2/")"
    if ! printf '%s' "$candidate" | grep -q '^id:'; then
        echo "[iter $iter] Ollama did not return a valid rule; reject"
        record "$iter" reject "$b_recall" "$b_prec" "no-rule"
        return 2
    fi

    # give the candidate a unique id (Ollama often leaves the NNN placeholder or
    # reuses an id); use the iteration number so learned rules never collide.
    candidate="$(printf '%s\n' "$candidate" | sed -E "s/^id:.*/id: LEARNED-$(date +%s)-${iter}/")"

    # ANTI-OVERFIT: reject a regex that just memorises the training value.
    # A rule like `regex: AKIAIOSFODNN7EXAMPLE` or `regex: 1234` would "find" the
    # one training case and generalise to nothing. Require the pattern to be a
    # GENERALISING matcher (character classes + quantifiers), not a long literal
    # copied from the secret, and not present verbatim in the corpus.
    if ! regex_generalises "$candidate"; then
        echo "[iter $iter] REJECT (anti-overfit: regex is too literal/specific)"
        record "$iter" reject "$b_recall" "$b_prec" "overfit-regex"
        return 2
    fi

    # write candidate to a temp rules dir and re-measure
    local tmpd; tmpd="$(mktemp -d)"; cp -r "$LEARNED/." "$tmpd/" 2>/dev/null || true
    printf '%s\n' "$candidate" > "$tmpd/candidate.rule"
    read -r c_recall c_prec c_f1 c_fn < <(measure "$BASE_RULES:$tmpd")
    echo "[iter $iter] candidate: recall=$c_recall precision=$c_prec f1=$c_f1 fn=$c_fn"

    # THE GATE (benchmark decides): recall must rise on train, precision stay
    # >= floor, AND the candidate must not collapse on the holdout split (which
    # Ollama never saw). A rule that helps train but not holdout is overfitting.
    read -r h_recall h_prec _ _ < <(measure "$BASE_RULES:$tmpd" holdout)
    echo "[iter $iter] holdout:   recall=$h_recall precision=$h_prec"
    local accept
    accept="$(python3 -c "
b_r,b_p,c_r,c_p = float('$b_recall'),float('$b_prec'),float('$c_recall'),float('$c_prec')
h_r,h_p        = float('$h_recall'),float('$h_prec')
train_ok = c_r > b_r and c_p >= min($MIN_PRECISION, b_p)
# generalisation: holdout precision must not fall below the floor (a rule that
# fires as noise on unseen repos is rejected even if it lifts train recall).
holdout_ok = h_p >= $MIN_PRECISION
print('yes' if (train_ok and holdout_ok) else 'no')")"

    if [ "$accept" = "yes" ]; then
        local id; id="$(printf '%s' "$candidate" | awk -F': *' '/^id:/{print $2;exit}')"
        cp "$tmpd/candidate.rule" "$LEARNED/${id:-learned-$iter}.rule"
        echo "[iter $iter] ACCEPT -> rules/learned/${id:-learned-$iter}.rule (holdout recall=$h_recall)"
        record "$iter" accept "$c_recall" "$c_prec" "${id} holdout_recall=$h_recall"
        rm -rf "$tmpd"; return 0
    else
        echo "[iter $iter] REJECT (gate: train or holdout criteria not met)"
        record "$iter" reject "$c_recall" "$c_prec" "gate holdout_prec=$h_prec"
        rm -rf "$tmpd"; return 2
    fi
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
    # Convergence: stop after this many consecutive rejects (no accepted change).
    # Re-proposing the same FN cluster without progress just burns time/tokens.
    MAX_STALL="${KEUR_MAX_STALL:-8}"
    stall=0
    for i in $(seq 1 "$N"); do
      iterate_once "$i"; rc=$?
      case "$rc" in
        0) stall=0 ;;                         # accepted: progress
        1) echo "converged: nothing left to learn"; break ;;
        *) stall=$((stall+1)) ;;              # rejected
      esac
      if [ "$stall" -ge "$MAX_STALL" ]; then
        echo "converged: $stall consecutive rejects (no improvement); stopping"
        break
      fi
    done
    ;;
  *) echo "usage: eval/optimise.sh {iterate [N]|baseline}"; exit 1 ;;
esac
