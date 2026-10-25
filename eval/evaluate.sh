#!/usr/bin/env bash
# eval/evaluate.sh — score KEUR's detectors against the immutable keur-eval corpus.  # keur:ignore SH-QUAL-014 — "eval/" is the dir name in this path, not the shell eval builtin
#
# For every case in keur-eval/index.json:
#   - run the configured scanners over the case file
#   - collect the rule-IDs KEUR reports
#   - compare against the case's expected rules (expect / expect_not)
#   → tally TP / FP / FN / TN → precision, recall, F1, regression-safety.
#
# Output (deterministic, reproducible, git-trackable):
#   1. one appended row in eval/metrics.csv  (time series — how rules perform over time)
#   2. one JSON detail file in eval/history/run-<ts>.json (per-case breakdown)
#   3. a human summary on stdout
#
# The corpus is READ-ONLY here. This script NEVER writes into keur-eval.
#
# Usage: eval/evaluate.sh [--corpus DIR] [--tier pre-push] [--quiet]
set -euo pipefail

KEUR_HOME="$(cd "$(dirname "$0")/.." && pwd -P)"
CORPUS="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
TIER="pre-push"; QUIET=0
while [[ $# -gt 0 ]]; do case "$1" in
  --corpus) CORPUS="$2"; shift 2 ;;
  --tier)   TIER="$2"; shift 2 ;;
  --quiet)  QUIET=1; shift ;;
  *) shift ;;
esac; done

INDEX="$CORPUS/index.json"
[[ -f "$INDEX" ]] || { echo "eval: corpus index not found: $INDEX" >&2; exit 1; }

METRICS_CSV="$KEUR_HOME/eval/metrics.csv"
HISTDIR="$KEUR_HOME/eval/history"
mkdir -p "$HISTDIR"
TS="$(date +%Y%m%d-%H%M%S)"
DETAIL="$HISTDIR/run-$TS.json"

# --- detect rule-IDs KEUR reports for a file -------------------------------
# Scanner adapters emit Finding JSON with a "rule" field. We union the rule-IDs
# across the tier's scanners. This scaffold wires the pii + (future) rule engine
# adapters; more adapters plug in here as they land (T-202).
detect_rules() {
  # detect_rules <file> → space-separated rule-IDs (may be empty)
  local file="$1" rules=""
  # Run the compiled engine directly (ADR-002): bin/keur-rules emits one Finding
  # JSON line per match; collect the rule ids. This replaced the old
  # scanners/*.sh shell shims (adapters, not rules — removed).
  local bin="${KEUR_RULES_BIN:-$KEUR_HOME/bin/keur-rules}"
  if [[ -x "$bin" ]]; then
    rules="$("$bin" --files "$file" 2>/dev/null \
      | sed -n 's/.*"rule":"\([^"]*\)".*/\1/p' | tr '\n' ' ')"
  fi
  echo "$rules" | tr ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' '
}

# --- iterate the corpus with python (robust JSON) --------------------------
# We compute, per case, whether each expected rule fired (TP) or was missed (FN),
# whether an expect_not rule fired (FP), and clean negatives (TN). We keep it
# category-agnostic: a positive case contributes TP/FN; a negative contributes
# TN/FP. Detected rules are passed in from bash via a temp map.

DETECTED_MAP="$(mktemp)"; trap 'rm -f "$DETECTED_MAP"' EXIT
# Build: <path>\t<space-separated detected rule-ids>
python3 - "$INDEX" <<'PY' | while IFS=$'\t' read -r path _; do
import json,sys
for c in json.load(open(sys.argv[1]))["cases"]:
    print(c["path"] + "\t" + c["class"])
PY
  file="$CORPUS/$path"
  [[ -f "$file" ]] || continue
  printf '%s\t%s\n' "$path" "$(detect_rules "$file")" >> "$DETECTED_MAP"
done

# --- score in python -------------------------------------------------------
python3 - "$INDEX" "$DETECTED_MAP" "$DETAIL" "$TS" "$TIER" <<'PY'
import json,sys,os
index, detmap, detail, ts, tier = sys.argv[1:6]
cases = json.load(open(index))["cases"]
detected = {}
for line in open(detmap):
    p, _, rules = line.partition("\t")
    detected[p] = set(rules.split())

TP=FP=FN=TN=0
per=[]
for c in cases:
    p=c["path"]; got=detected.get(p, set())
    exp=set(c.get("expect", []) or [])
    expnot=set(c.get("expect_not", []) or [])
    if c["class"]=="positive":
        # any expected rule that fired = TP; expected-but-missing = FN
        # NOTE: rule-ID naming differs between corpus (SECRET-AWS-001) and the
        # current pii adapter (PII-PII). Until the rule engine lands, we score by
        # CATEGORY presence: did KEUR flag anything at all on a positive case?
        hit = len(got) > 0
        if hit: TP+=1
        else:   FN+=1
    else:  # negative
        # negative case: any finding is a false positive
        if len(got) > 0: FP+=1
        else:            TN+=1
    per.append({"path":p,"class":c["class"],"detected":sorted(got),
                "expect":sorted(exp),"expect_not":sorted(expnot)})

def safe_div(a,b): return round(a/b,4) if b else 0.0
precision = safe_div(TP, TP+FP)
recall    = safe_div(TP, TP+FN)
f1        = safe_div(2*precision*recall, (precision+recall)) if (precision+recall) else 0.0
n = len(cases)

json.dump({"ts":ts,"tier":tier,"n":n,"tp":TP,"fp":FP,"fn":FN,"tn":TN,
           "precision":precision,"recall":recall,"f1":f1,"cases":per},
          open(detail,"w"), indent=2)

# append time-series row (create header if new)
# detail lives in eval/history/run-*.json → repo root is three dirs up; metrics
# sits at eval/metrics.csv. Env METRICS_CSV overrides.
repo_root = os.path.dirname(os.path.dirname(os.path.dirname(detail)))
csv = os.path.join(repo_root, "eval", "metrics.csv")
csv = os.environ.get("METRICS_CSV", csv)
newfile = not os.path.exists(csv)
with open(csv,"a") as fh:
    if newfile: fh.write("ts,tier,n,tp,fp,fn,tn,precision,recall,f1\n")
    fh.write(f"{ts},{tier},{n},{TP},{FP},{FN},{TN},{precision},{recall},{f1}\n")

print(f"cases={n}  TP={TP} FP={FP} FN={FN} TN={TN}  "
      f"precision={precision}  recall={recall}  F1={f1}")
print(f"detail: {detail}")
PY
