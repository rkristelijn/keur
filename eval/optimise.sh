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
NATIVE="$KEUR_HOME/bin/keur-rules"          # the compiled rule engine (slop test)
mkdir -p "$LEARNED" "$HERE/history"

# Which split the optimiser trains on. Holdout stays unseen during training and
# is only measured for a generalisation report (overfitting check).
TRAIN_SPLIT="${KEUR_TRAIN_SPLIT:-train}"

# Which measurement source is the training signal:
#   corpus   -> eval/measure.sh   (proven-tool ground truth; recall-focused)
#   keureval -> eval/keureval.sh  (keur-eval CONTRACT; scores FALSE POSITIVES on
#                                  negative cases, so the gate can punish noise)
# Both write the same eval/corpus/measure.json shape, so the rest is identical.
KEUR_EVAL_SOURCE="${KEUR_EVAL_SOURCE:-corpus}"
case "$KEUR_EVAL_SOURCE" in
    keureval) EVAL_CMD="$HERE/keureval.sh" ;;
    corpus|*) EVAL_CMD="$HERE/measure.sh" ;;
esac

# measure with a given rules dir -> echoes "recall precision f1 fn" (space-sep)
measure() {
    local dirs="$1"; local split="${2:-$TRAIN_SPLIT}"
    KEUR_SPLIT="$split" KEUR_RULES_DIRS="$dirs" "$EVAL_CMD" >/dev/null 2>&1
    python3 -c "import json;d=json.load(open('$HERE/corpus/measure.json'));print(d['recall'],d['precision'],d['f1'],d['fn'])"
}

# sample_fn <rules-dir> <index> -> one missed finding, chosen at RANDOM from
# the distinct missed clusters (seeded per iteration + PID) so each iteration
# attacks a different one instead of re-proposing rules for the same hard case
# forever — and so a phase restart does not always begin at the same cluster.
sample_fn() {
    local idx="${2:-0}"
    KEUR_SPLIT="$TRAIN_SPLIT" KEUR_RULES_DIRS="$1" "$EVAL_CMD" --fn 2>/dev/null \
      | awk '/sample missed/{f=1;next} f&&NF{print}' \
      | awk -v seed="${idx}${RANDOM}$$" 'NR>=1{a[NR]=$0} END{if(NR==0)exit; srand(seed); print a[int(rand()*NR)+1]}'
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

# is_slop <candidate-rule-text> <fn-line> -> exit 0 (SLOP) if the candidate
# fires on a case but NOT on a reformatted (perturbed) variant of it. That means
# the rule learned the FORM (the corpus's tidy "Label: value" layout), not the
# CONCEPT (the sensitive value / dangerous call). Perturbation keeps the
# semantic content, strips the structure. A concept-bound rule still fires on
# the perturbed variant; a form-bound rule misses -> slop. Exit 1 = genuine or
# undeterminable.
#
# NOTE: because scoring is at CATEGORY granularity, the candidate rarely fires
# on the exact TARGET case (fn_line field 3). So we search the target's category
# directory for the FIRST case the candidate actually fires on, and perturb that
# — testing the rule where it genuinely matches, not where we hoped it would.
is_slop() {
    local cand="$1"; local fn_line="$2"
    local ref; ref=$(echo "$fn_line" | awk '{print $3}')
    # Determine the directory of cases to search (the category dir the target is
    # in). Resolve the target to an absolute path first.
    local target=""
    for base in "$KEUR_HOME/eval/corpus/repos" "${KEUR_EVAL:-$HOME/git/hub/keur-eval}"; do
        for c in "$base/$ref" "$base/${ref#*/}"; do
            [ -f "$c" ] && { target="$c"; break; }
        done
        [ -n "$target" ] && break
    done
    [ -z "$target" ] && return 1   # can't locate -> can't judge -> not slop

    local sd; sd="$(mktemp -d)"
    printf '%s\n' "$cand" > "$sd/r.rule"
    local id; id="$(printf '%s' "$cand" | awk -F': *' '/^id:/{print $2;exit}')"
    local dir; dir="$(dirname "$target")"

    # Find the first case in this category dir where the candidate actually fires
    # (isolated: empty KEUR_HOME so only THIS rule loads).
    local hit="" f ext
    for f in "$dir"/*; do
        [ -f "$f" ] || continue
        ext="${f##*.}"
        if KEUR_HOME="" KEUR_RULES_DIRS="$sd" "$NATIVE" --files "$f" 2>/dev/null | grep -q "\"$id\""; then
            hit="$f"; break
        fi
    done
    [ -z "$hit" ] && { rm -rf "$sd"; return 1; }   # fires nowhere here -> can't judge

    # Perturb the case it DID fire on, keep the same extension, re-test.
    ext="${hit##*.}"
    "$HERE/perturb.sh" "$hit" > "$sd/pert.$ext" 2>/dev/null
    local on_pert
    on_pert=$(KEUR_HOME="" KEUR_RULES_DIRS="$sd" "$NATIVE" --files "$sd/pert.$ext" 2>/dev/null | grep -c "\"$id\"")
    rm -rf "$sd"

    # slop iff it fired on the (real) original but vanished on the perturbed one
    [ "$on_pert" -eq 0 ]
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
    # The 3rd field is repo/file (corpus) or a keur-eval relative path. Try to
    # resolve it to a real file and show the model the ACTUAL missed content —
    # without it, the model only sees the category NAME and guesses (e.g. it
    # read "cbi" as "callback" and wrote a useless rule). Showing the evidence is
    # what turns opaque category labels into catchable patterns.
    local ref; ref=$(echo "$fn_line" | awk '{print $3}')
    local snippet="" example_file=""
    for base in "$KEUR_HOME/eval/corpus/repos" "${KEUR_EVAL:-$HOME/git/hub/keur-eval}"; do
        # ref may be "repo/path" (corpus) or "positive/cat/file" (keur-eval)
        for cand in "$base/$ref" "$base/${ref#*/}"; do
            [ -f "$cand" ] && { example_file="$cand"; break; }
        done
        [ -n "$example_file" ] && break
    done
    if [ -n "$example_file" ]; then
        # first ~25 non-empty lines, stripped of KEUR-EXPECT annotations
        snippet=$(grep -vE 'KEUR-(EXPECT|CATEGORY|NOTE)' "$example_file" 2>/dev/null \
                  | grep -v '^[[:space:]]*$' | head -25)
    fi
    # Short human meaning for the non-obvious categories (the obvious ones like
    # sqli/secret the model already understands from its system prompt).
    local hint=""
    case "$cat" in
        cbi)  hint="CBI = Commercially/Business-sensitive Information: internal hostnames, private/internal IPs (e.g. 10.x, 192.168.x, 192.0.2.x), monetary figures, supplier/contract references." ;;
        pii)  hint="PII = Personally Identifiable Information: names+national IDs, phone numbers, email+address combos, dates of birth." ;;
        personal-docs) hint="personal-docs = private personal documents leaking into a repo: medical, financial, legal, or HR content about an individual." ;;
        cmdi) hint="CMDI = OS command injection: user input reaching os.system/exec/subprocess/backticks/shell." ;;
    esac

    cat <<EOF
A proven security tool found a "$cat" issue that our rule engine MISSED.
${hint:+Category meaning: $hint}

Here is the ACTUAL content the tool flagged (write a rule that would catch it,
and generalises to similar cases — do NOT hard-code these literal values):
--- BEGIN MISSED CASE ---
$snippet
--- END MISSED CASE ---

Write ONE keur .rule that catches this class of "$cat" issue and generalises to
other repos (not a memorised literal).

Start the file with:  id: LEARNED-$(echo "$cat" | tr a-z A-Z)-NNN
Use category: $cat, engine: pattern, and target these extensions:
  .js .php .rb .py .env .yml .json .ts .go .java .md .txt

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
    #
    # The holdout measure echoes "recall precision f1 fn". We ALSO need the raw
    # tp/fp so we can tell "precision is genuinely low" from "precision is 0.0
    # because nothing fired / the split is empty". A degenerate holdout (no
    # fires, or too few cases) gives precision=0.0 and would wrongly reject every
    # candidate — that was the bug that blocked the keur-eval source entirely.
    read -r h_recall h_prec _ _ < <(measure "$BASE_RULES:$tmpd" holdout)
    # Pull tp/fp from the holdout measure.json the previous call just wrote.
    read -r h_tp h_fp < <(python3 -c "import json;d=json.load(open('$HERE/corpus/measure.json'));print(d.get('tp',0),d.get('fp',0))")
    echo "[iter $iter] holdout:   recall=$h_recall precision=$h_prec (tp=$h_tp fp=$h_fp)"
    local accept
    accept="$(python3 -c "
b_r,b_p,c_r,c_p = float('$b_recall'),float('$b_prec'),float('$c_recall'),float('$c_prec')
h_p            = float('$h_prec')
h_tp, h_fp     = int('$h_tp'), int('$h_fp')
train_ok = c_r > b_r and c_p >= min($MIN_PRECISION, b_p)
# Generalisation check, but only when the holdout can actually MEASURE
# precision. If the candidate fired nothing on holdout (h_tp+h_fp == 0),
# precision is undefined (reported as 0.0) — that is NOT evidence of noise, so
# we do not punish it; fall back to the train-precision gate. Only when the
# rule DID fire on holdout do we require it to stay clean.
holdout_measurable = (h_tp + h_fp) > 0
holdout_ok = (h_p >= $MIN_PRECISION) if holdout_measurable else True
print('yes' if (train_ok and holdout_ok) else 'no')")"

    if [ "$accept" = "yes" ]; then
        local id; id="$(printf '%s' "$candidate" | awk -F': *' '/^id:/{print $2;exit}')"
        # SLOP TEST (soft marker, option B): the candidate passed the benchmark,
        # but does it generalise beyond the training case's FORM? Perturb the
        # target case (keep the value, strip the layout) and re-test. If it fires
        # on the original but not the perturbed variant, it learned the form —
        # keep it as WEAK evidence: tag slop: true and cap confidence below
        # block-at so it can only ever WARN, never BLOCK. Evidence is preserved;
        # its weight is degraded. (keur.toml block-at = 0.85; cap at 0.50.)
        local slop_note=""
        if is_slop "$candidate" "$fn"; then
            slop_note=" slop=true"
            echo "[iter $iter] SLOP: fires on original but not on perturbed variant -> soft-marking (cap confidence, WARN-only)"
            # write the candidate with slop:true and a capped confidence
            printf '%s\n' "$candidate" \
              | sed -E 's/^confidence:.*/confidence: 0.50/' > "$tmpd/candidate.rule"
            # ensure a slop marker line exists (append if absent)
            grep -q '^slop:' "$tmpd/candidate.rule" || echo "slop: true" >> "$tmpd/candidate.rule"
            # if there was no confidence line at all, add the cap
            grep -q '^confidence:' "$tmpd/candidate.rule" || echo "confidence: 0.50" >> "$tmpd/candidate.rule"
        fi
        cp "$tmpd/candidate.rule" "$LEARNED/${id:-learned-$iter}.rule"
        echo "[iter $iter] ACCEPT -> rules/learned/${id:-learned-$iter}.rule (holdout recall=$h_recall)${slop_note}"
        record "$iter" accept "$c_recall" "$c_prec" "${id} holdout_recall=$h_recall${slop_note}"
        rm -rf "$tmpd"; return 0
    else
        echo "[iter $iter] REJECT (gate: train or holdout criteria not met)"
        record "$iter" reject "$c_recall" "$c_prec" "gate holdout_prec=$h_prec"
        rm -rf "$tmpd"; return 2
    fi
}

record() {
    # Tag each record with the active eval source (corpus|keureval) so the
    # monitor can separate runs instead of mixing their recall scales.
    KEUR_EVAL_SOURCE="$KEUR_EVAL_SOURCE" python3 -c "import json,sys,time,os;print(json.dumps({'iter':int(sys.argv[1]),'ts':time.strftime('%Y-%m-%dT%H:%M:%S'),'decision':sys.argv[2],'recall':float(sys.argv[3]),'precision':float(sys.argv[4]),'note':sys.argv[5],'source':os.environ.get('KEUR_EVAL_SOURCE','corpus')}))" "$@" >> "$HIST"
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
