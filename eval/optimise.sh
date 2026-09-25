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
if [[ -z "${MODEL:-}" ]]; then
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
ATTEMPT_LOG="$HERE/history/attempts.tsv"     # per-attempt trace (retry analysis, TSV — no python)
BASE_RULES="$KEUR_HOME/rules"               # keur's own rules (always loaded)
NATIVE="$KEUR_HOME/bin/keur-rules"          # the compiled rule engine (slop test)
COMPOSITE="$HERE/composite-candidates"      # slop rules kept as composite building blocks (NOT loaded standalone)
mkdir -p "$COMPOSITE"
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
    # ADR-027: measure at file granularity so within-category gaps are real FNs.
    KEUR_MATCH_GRANULARITY="${KEUR_MATCH_GRANULARITY:-file}" \
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
            [[ -f "$c" ]] && { target="$c"; break; }
        done
        [[ -n "$target" ]] && break
    done
    [[ -z "$target" ]] && return 1   # can't locate -> can't judge -> not slop

    local sd; sd="$(mktemp -d)"
    printf '%s\n' "$cand" > "$sd/r.rule"
    local id; id="$(printf '%s' "$cand" | awk -F': *' '/^id:/{print $2;exit}')"
    local dir; dir="$(dirname "$target")"

    # Find the first case in this category dir where the candidate actually fires
    # (isolated: empty KEUR_HOME so only THIS rule loads).
    local hit="" f ext
    for f in "$dir"/*; do
        [[ -f "$f" ]] || continue
        ext="${f##*.}"
        if KEUR_HOME="" KEUR_RULES_DIRS="$sd" "$NATIVE" --files "$f" 2>/dev/null | grep -q "\"$id\""; then
            hit="$f"; break
        fi
    done
    [[ -z "$hit" ]] && { rm -rf "$sd"; return 1; }   # fires nowhere here -> can't judge

    # Perturb the case it DID fire on, keep the same extension, re-test.
    ext="${hit##*.}"
    "$HERE/perturb.sh" "$hit" > "$sd/pert.$ext" 2>/dev/null
    local on_pert
    on_pert=$(KEUR_HOME="" KEUR_RULES_DIRS="$sd" "$NATIVE" --files "$sd/pert.$ext" 2>/dev/null | grep -c "\"$id\"")
    rm -rf "$sd"

    # slop iff it fired on the (real) original but vanished on the perturbed one
    [[ "$on_pert" -eq 0 ]]
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

# good_example_for <category> — a REAL, generalising rule shape from keur's own
# rule set, shown to the model as "here is what good looks like for this
# category". Showing a concrete good example steers far better than abstract
# tips (the retry loop proved the model repeats the same mistake when only told
# "use character classes"). Each example is keyword-anchored, length/structure-
# bounded, and generalises — never a memorised literal.
good_example_for() {
    case "$1" in
      secret) echo '(?i)(api[_-]?key|token|secret|password)\s*[:=]\s*["'"'"'][^"'"'"']{12,}["'"'"']  # keyword + either separator + a 12+ char quoted value' ;;
      cmdi)   echo '\b(?:os\.system|subprocess\.(?:run|call)|exec)\s*\([^)]*[A-Za-z_]\w*[^)]*\)  # a shell sink called with a variable (not a literal) arg; add exclude: for the literal-only safe case' ;;
      sqli)   echo '(?i)\b(?:execute|query|prepare)\s*\(\s*["'"'"'][^"'"'"']*\bselect\b[^"'"'"']*["'"'"']\s*[%+.]  # a SELECT string that is then concatenated/formatted (%, +, .format)' ;;
      iac)    echo '(?im)^\s*-?\s*uses:\s*[\w.-]+/[\w.-]+@(v?\d[\w.-]*|main|latest)  # a workflow action pinned to a mutable tag; exclude: @[0-9a-f]{40} for the safe SHA pin' ;;
      sast)   echo '\bnew\s+RegExp\s*\(\s*[A-Za-z_$][\w$.]*  # a sink built from a variable; add exclude: for the literal-argument safe case' ;;
      *)      echo '(?i)\bKEYWORD\s*[:=]\s*["'"'"'][^"'"'"']{N,}["'"'"']  # anchor on a keyword, bound the value length, use a negated class instead of .*' ;;
    esac
}

build_prompt() {
    local fn_line="$1"
    local feedback="${2:-}"   # retry: the reason the previous attempt was rejected
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
            [[ -f "$cand" ]] && { example_file="$cand"; break; }
        done
        [[ -n "$example_file" ]] && break
    done
    if [[ -n "$example_file" ]]; then
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

    # Build the retry-feedback block as a plain variable (NOT inside a
    # ${feedback:+...} expansion in the heredoc — a literal '}' in the text would
    # close that expansion early and break the whole prompt). Only on retries.
    local retry_block=""
    if [[ -n "$feedback" ]]; then
        local good; good="$(good_example_for "$cat")"
        retry_block="
YOUR PREVIOUS ATTEMPT WAS REJECTED. Reason: $feedback

How to fix it this time:
  - OVERFIT (too literal): do NOT copy any value from the case. Match the
    STRUCTURE with character classes + quantifiers so it fires on OTHER repos too.
  - PRECISION dropped: too broad. Anchor it (word boundary, a keyword, an
    assignment, a specific sink name) or add an exclude: regex for the safe case.
  - RECALL did not rise: it matched nothing new. Widen it just enough to catch
    this case AND its siblings.

A GOOD $cat rule shape to model yours on (adapt, do not copy):
  regex: $good

Keep what was right; change only what the reason says failed.
"
    fi

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
Use category: $cat and target the relevant extensions (a subset of:
  .js .php .rb .py .env .yml .yaml .json .ts .go .java) — pick only those that fit.

Choose the SIMPLEST engine that fits (do not default to pattern blindly):
  - engine: pattern     one dangerous line; add exclude: <regex> to subtract the safe case.
  - engine: multiline   the evidence SPANS lines (assign here, use there): set
                        within: window and window: 3, use [\\s\\S]*? to span.
  - engine: absence     something REQUIRED is missing from the file.
  - engine: composite   combine other rules: composite: { all: ID-A ID-B } / any / not.
If the issue only matters for a framework/version, add a requires: guard
(lang:/framework:/version:) instead of hard-coding it into the regex.
${retry_block}
Output ONLY the rule file, starting with "id:". No markdown fences, no prose.
EOF
}

iterate_once() {
    local iter="$1"
    # snapshot the currently-accepted learned rules alongside base
    local dirs="$BASE_RULES:$LEARNED"
    read -r b_recall b_prec b_f1 b_fn < <(measure "$dirs")
    echo "[iter $iter] baseline: recall=$b_recall precision=$b_prec f1=$b_f1 fn=$b_fn"
    if [[ "$b_fn" = "0" ]]; then echo "[iter $iter] no FN left; nothing to learn"; return 1; fi

    local fn; fn="$(sample_fn "$dirs" "$iter")"
    [[ -z "$fn" ]] && { echo "[iter $iter] no FN sample"; return 1; }
    echo "[iter $iter] target FN: $fn"

    # RETRY LOOP (up to KEUR_MAX_ATTEMPTS): give Ollama several shots at the SAME
    # missed case, feeding back WHY the previous attempt was rejected — the same
    # way a human iterates on a regex with the test result in hand. One-shot was
    # the old behaviour and threw away fixable near-misses. @see TODO retry-loop
    local max_attempts="${KEUR_MAX_ATTEMPTS:-4}"
    local feedback="" attempt=1
    while [[ "$attempt" -le "$max_attempts" ]]; do
      echo "[iter $iter] attempt $attempt/$max_attempts${feedback:+ (retry: ${feedback})}"
      # ask Ollama for a candidate rule (with feedback on retries)
      local candidate; candidate="$(ollama "$(build_prompt "$fn" "$feedback")")"
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
        log_attempt "$iter" "$attempt" "$(echo "$fn"|awk '{print $2}')" retry no-rule ""
        if [[ "$attempt" -lt "$max_attempts" ]]; then
            feedback="you did not return a valid .rule starting with 'id:'"; attempt=$((attempt+1)); continue
        fi
        echo "[iter $iter] Ollama did not return a valid rule; reject"
        record "$iter" reject "$b_recall" "$b_prec" "no-rule"
        return 2
    fi

    # give the candidate a unique id (Ollama often leaves the NNN placeholder or
    # reuses an id); use the iteration number so learned rules never collide.
    candidate="$(printf '%s\n' "$candidate" | sed -E "s/^id:.*/id: LEARNED-$(date +%s)-${iter}/")"
    local cand_rx; cand_rx="$(printf '%s\n' "$candidate" | awk -F'- regex: ' '/- regex:/{print $2;exit}')"

    # ANTI-OVERFIT: reject a regex that just memorises the training value.
    # A rule like `regex: AKIAIOSFODNN7EXAMPLE` or `regex: 1234` would "find" the  # keur:ignore SEC-010
    # one training case and generalise to nothing. Require the pattern to be a
    # GENERALISING matcher (character classes + quantifiers), not a long literal
    # copied from the secret, and not present verbatim in the corpus.
    if ! regex_generalises "$candidate"; then
        log_attempt "$iter" "$attempt" "$(echo "$fn"|awk '{print $2}')" retry overfit-regex "$cand_rx"
        if [[ "$attempt" -lt "$max_attempts" ]]; then
            feedback="the regex was too literal/specific (memorised the example). Use character classes + quantifiers to match the FAMILY, not the exact value"; attempt=$((attempt+1)); continue
        fi
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
FLOOR          = $MIN_PRECISION
# THE GATE. Recall must rise. Precision must clear the ABSOLUTE floor AND not
# regress below the baseline. The old code used min(FLOOR, b_p), which LOWERED
# the floor once precision had already dipped below it — so one bad rule opened
# the door to an accept-everything spiral (recall->1.0 while precision->0.4).
# The floor is a hard limit; it never moves down.
train_ok = (c_r > b_r) and (c_p >= FLOOR) and (c_p >= b_p - 1e-9)
# Generalisation check, but only when the holdout can actually MEASURE
# precision. If the candidate fired nothing on holdout (h_tp+h_fp == 0),
# precision is undefined (reported as 0.0) — that is NOT evidence of noise, so
# we do not punish it; fall back to the train-precision gate. Only when the
# rule DID fire on holdout do we require it to stay clean.
holdout_measurable = (h_tp + h_fp) > 0
holdout_ok = (h_p >= FLOOR) if holdout_measurable else True
print('yes' if (train_ok and holdout_ok) else 'no')")"

    if [[ "$accept" = "yes" ]]; then
        local id; id="$(printf '%s' "$candidate" | awk -F': *' '/^id:/{print $2;exit}')"
        # SLOP TEST: the candidate passed the benchmark, but does it generalise
        # beyond the training case's FORM? Perturb the case it fires on (keep the
        # value, strip the layout) and re-test. If it fires on the original but
        # not the perturbed variant, it learned the FORM, not the CONCEPT.
        #
        # Such a rule is NOT discarded and NOT loaded as a standalone rule (that
        # would add WARN noise). It is a weak SELECTOR — valuable only when it
        # CORROBORATES another selector in the same file/block. We preserve it in
        # composite-candidates/ as a building block for future composite rules
        # (see TODO.md "composite rules via corroboration" + corroboration-bonus
        # in config/keur.toml). It keeps slop:true + a note recording the case.
        if is_slop "$candidate" "$fn"; then
            local cand_out; cand_out="$COMPOSITE/${id:-slop-$iter}.rule"
            printf '%s\n' "$candidate" > "$cand_out"
            grep -q '^slop:' "$cand_out" || echo "slop: true" >> "$cand_out"
            echo "# composite-candidate: fires on form, not concept; kept for corroboration. from FN: $fn" >> "$cand_out"
            echo "[iter $iter] SLOP -> composite-candidates/${id:-slop-$iter}.rule (weak selector, kept for future composite rules)"
            record "$iter" accept "$c_recall" "$c_prec" "${id} holdout_recall=$h_recall slop=true composite-candidate"
            rm -rf "$tmpd"; return 0
        fi
        cp "$tmpd/candidate.rule" "$LEARNED/${id:-learned-$iter}.rule"
        echo "[iter $iter] ACCEPT -> rules/learned/${id:-learned-$iter}.rule (holdout recall=$h_recall)"
        log_attempt "$iter" "$attempt" "$(echo "$fn"|awk '{print $2}')" accept accepted "$cand_rx"
        record "$iter" accept "$c_recall" "$c_prec" "${id} holdout_recall=$h_recall"
        rm -rf "$tmpd"; return 0
    else
        # Build SPECIFIC feedback so the retry is steered, not blind: tell Ollama
        # exactly which criterion failed (recall didn't rise / precision below
        # floor / holdout noisy), with the numbers, so it can fix that one thing.
        local why
        why="$(python3 -c "
b_r,b_p,c_r,c_p=float('$b_recall'),float('$b_prec'),float('$c_recall'),float('$c_prec')
h_p=float('$h_prec'); h_tp,h_fp=int('$h_tp'),int('$h_fp'); FLOOR=$MIN_PRECISION
msgs=[]
if not (c_r > b_r):
    msgs.append(f'recall did not rise (was {b_r:.3f}, your rule {c_r:.3f}) — it matched nothing NEW; broaden the pattern to catch this case and similar ones')
if c_p < FLOOR or c_p < b_p-1e-9:
    msgs.append(f'precision dropped too far ({c_p:.3f}, floor {FLOOR}, baseline {b_p:.3f}) — the rule is too broad and fires on safe code; tighten it or add an exclude:')
if (h_tp+h_fp)>0 and h_p < FLOOR:
    msgs.append(f'on the holdout set precision was {h_p:.3f} (fp={h_fp}) — it generalises as NOISE; make it more specific')
print('; '.join(msgs) or 'the benchmark did not improve')")"
        if [[ "$attempt" -lt "$max_attempts" ]]; then
            feedback="$why"; attempt=$((attempt+1)); rm -rf "$tmpd"; continue
        fi
        echo "[iter $iter] REJECT (gate: train or holdout criteria not met)"
        record "$iter" reject "$c_recall" "$c_prec" "gate holdout_prec=$h_prec"
        rm -rf "$tmpd"; return 2
    fi
  done
}

record() {
    # Tag each record with the active eval source (corpus|keureval) so the
    # monitor can separate runs instead of mixing their recall scales.
    KEUR_EVAL_SOURCE="$KEUR_EVAL_SOURCE" python3 -c "import json,sys,time,os;print(json.dumps({'iter':int(sys.argv[1]),'ts':time.strftime('%Y-%m-%dT%H:%M:%S'),'decision':sys.argv[2],'recall':float(sys.argv[3]),'precision':float(sys.argv[4]),'note':sys.argv[5],'source':os.environ.get('KEUR_EVAL_SOURCE','corpus')}))" "$@" >> "$HIST"
}

# log_attempt <iter> <attempt> <cat> <outcome> <reason> <regex>
# Per-ATTEMPT trace for retry analysis. Deliberately SHELL-ONLY (no python):
# tab-separated, so we add no new language dependency. Will move to a small
# compiled keur-jsonl util (TODO: "NO PYTHON in the eval scripting layer");
# until then this stays plain TSV that jq/awk can read.
log_attempt() {
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$(date +%Y-%m-%dT%H:%M:%S)" "$1" "$2" "$3" "$4" "$5" "${6:-}" >> "$ATTEMPT_LOG"
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
    # Stop files: honour both the supervisor's STOP and the loop stop-file, so
    # `keur learn --stop` (and supervise.sh stop) end the run promptly between
    # iterations, not only at a phase boundary.
    STOP_SUP="$HERE/.run/STOP"; STOP_LOOP="/tmp/stop-keur-eval-loop"
    for i in $(seq 1 "$N"); do
      if [[ -f "$STOP_SUP" ]] || [[ -f "$STOP_LOOP" ]]; then
        echo "stop requested; ending iterate"; break
      fi
      iterate_once "$i"; rc=$?
      case "$rc" in
        0) stall=0 ;;                         # accepted: progress
        1) echo "converged: nothing left to learn"; break ;;
        *) stall=$((stall+1)) ;;              # rejected
      esac
      if [[ "$stall" -ge "$MAX_STALL" ]]; then
        echo "converged: $stall consecutive rejects (no improvement); stopping"
        break
      fi
    done
    ;;
  *) echo "usage: eval/optimise.sh {iterate [N]|baseline}"; exit 1 ;;
esac
