#!/usr/bin/env bash
# eval/measure.sh — measure keur's rule engine against the proven-tool ground
# truth. This is the TRAINING SIGNAL: how much of what the proven tools find
# does keur reproduce, and how much noise does it add?
#
# Matching is at file+category granularity (line numbers differ between tools):
#   TP = ground truth has a finding in (repo,file,category) AND keur fires there
#   FN = ground truth has one, keur misses it        -> what to LEARN
#   FP = keur fires where ground truth has nothing    -> noise to avoid
#
# Recall  = TP / (TP+FN)   (how much of the baseline keur reproduces)
# Precision = TP / (TP+FP) (how little noise keur adds)
#
# Usage:
#   eval/measure.sh            print metrics + write eval/corpus/measure.json
#   eval/measure.sh --fn       list the FN clusters (what the optimiser targets)
#
# Env: KEUR_RULES_DIRS (rule dirs for keur-rules), defaults to keur's own rules.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
export KEUR_HOME
CACHE="$HERE/corpus/repos"
GT="$HERE/corpus/groundtruth.jsonl"
OUT="$HERE/corpus/measure.json"
NATIVE="$KEUR_HOME/bin/keur-rules"

[ -f "$GT" ]     || { echo "no ground truth; run: eval/buildgt.sh" >&2; exit 1; }
[ -x "$NATIVE" ] || { echo "keur-rules not built; run: (cd src/rules && make)" >&2; exit 1; }

MODE="metrics"; [ "${1:-}" = "--fn" ] && MODE="fn"

# 1. Run keur over every repo, collect its findings as JSONL tagged with repo.
KEUR_FINDINGS="$(mktemp)"; trap 'rm -f "$KEUR_FINDINGS"' EXIT
for repo in "$CACHE"/*/; do
    [ -d "$repo" ] || continue
    name="$(basename "$repo")"
    "$NATIVE" --dir "$repo" 2>/dev/null | \
      python3 -c "import json,sys
for line in sys.stdin:
    line=line.strip()
    if not line: continue
    try: f=json.loads(line)
    except Exception: continue
    f['repo']='$name'
    print(json.dumps(f))" >> "$KEUR_FINDINGS"
done

# 2. Join keur findings with ground truth at (repo,file,category) granularity.
python3 - "$GT" "$KEUR_FINDINGS" "$OUT" "$MODE" <<'PY'
import json, sys, collections, os

gt_path, keur_path, out_path, mode = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

def key(f):
    # repo + category (Option A): a ground-truth "secret" in a repo is reproduced
    # if keur flags ANY secret in that repo. This measures category coverage, not
    # exact file overlap, because proven tools and keur detect by different
    # mechanisms (entropy/context vs keyword regex) and rarely hit the same file.
    return (f.get("repo",""), f.get("category",""))

gt = collections.defaultdict(list)
for line in open(gt_path):
    line=line.strip()
    if not line: continue
    f=json.loads(line); gt[key(f)].append(f)

keur = collections.defaultdict(list)
for line in open(keur_path):
    line=line.strip()
    if not line: continue
    f=json.loads(line); keur[key(f)].append(f)

gt_keys, keur_keys = set(gt), set(keur)
tp = gt_keys & keur_keys
fn = gt_keys - keur_keys      # ground truth found it, keur missed  -> LEARN
fp = keur_keys - gt_keys      # keur fired, ground truth did not     -> NOISE

def safe(a,b): return a/b if b else 0.0
recall    = safe(len(tp), len(tp)+len(fn))
precision = safe(len(tp), len(tp)+len(fp))
f1        = safe(2*precision*recall, precision+recall)

if mode == "fn":
    # FN clusters grouped by category: this is what the optimiser targets.
    by_cat = collections.Counter(k[1] for k in fn)
    print("FN clusters (ground truth keur misses), by category:")
    for cat, n in by_cat.most_common():
        print(f"  {n:4d}  {cat}")
    print("\nsample missed findings:")
    for k in list(fn)[:10]:
        ex = gt[k][0]
        print(f"  {ex.get('tool')}  {k[1]:8s}  {k[0]}/{ex.get('file')}")
else:
    result = {"tp":len(tp),"fn":len(fn),"fp":len(fp),
              "recall":round(recall,4),"precision":round(precision,4),"f1":round(f1,4),
              "fn_by_category": dict(collections.Counter(k[1] for k in fn))}
    json.dump(result, open(out_path,"w"), indent=2)
    print(f"keur vs ground truth:  TP={len(tp)} FN={len(fn)} FP={len(fp)}")
    print(f"  recall    {recall:.3f}  (baseline reproduced)")
    print(f"  precision {precision:.3f}  (low noise)")
    print(f"  f1        {f1:.3f}")
    if result["fn_by_category"]:
        print(f"  missed by category: {result['fn_by_category']}")
PY
