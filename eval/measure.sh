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

[[ -f "$GT" ]]     || { echo "no ground truth; run: eval/buildgt.sh" >&2; exit 1; }
[[ -x "$NATIVE" ]] || { echo "keur-rules not built; run: (cd src/rules && make)" >&2; exit 1; }

MODE="metrics"; [[ "${1:-}" = "--fn" ]] && MODE="fn"

# Split filter (Option: repo-level held-out set). KEUR_SPLIT = train|holdout|all
# (default all). The assignment lives in eval/corpus/split.txt as "repo<TAB>split";
# a repo not listed is treated as train. This lets the optimiser train on `train`
# while `holdout` is measured periodically to catch overfitting (Ollama never
# sees holdout during optimisation).
SPLIT="${KEUR_SPLIT:-all}"
SPLIT_FILE="$HERE/corpus/split.txt"

# Tool filter: restrict the ground truth to a proven subset. KEUR_GT_TOOLS is a
# comma-separated list of tool names (e.g. "gitleaks,semgrep"); empty = all
# tools. This is how we audit the baseline: measure only against tools we trust,
# so noise from a weaker source does not depress recall or mislead the loop.
GT_TOOLS="${KEUR_GT_TOOLS:-}"

# Category scope: restrict BOTH the ground truth AND keur's findings to a set of
# categories. KEUR_GT_CATEGORIES is comma-separated (e.g. "secret,sast,cmdi,sqli");
# empty = all categories. Unlike the tool filter, this applies to both sides so
# recall and precision are measured against the same scope — otherwise keur is
# penalised (as FP) for firing in a category the filtered baseline no longer has.
GT_CATS="${KEUR_GT_CATEGORIES:-}"
in_split() {
    # in_split <repo-name> -> 0 if it belongs to the active split
    [[ "$SPLIT" = "all" ]] && return 0
    local want; want="$(awk -v r="$1" '$1==r{print $2}' "$SPLIT_FILE" 2>/dev/null)"
    want="${want:-train}"   # unlisted repos default to train
    [[ "$want" = "$SPLIT" ]]
}

# 1. Run keur over every repo (in the active split), tag findings with repo.
KEUR_FINDINGS="$(mktemp)"; trap 'rm -f "$KEUR_FINDINGS"' EXIT
for repo in "$CACHE"/*/; do
    [[ -d "$repo" ]] || continue
    name="$(basename "$repo")"
    in_split "$name" || continue
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

# 2. Join keur findings with ground truth at (repo,category) granularity.
# Pass the split so the ground truth is filtered to the same repos as keur's.
python3 - "$GT" "$KEUR_FINDINGS" "$OUT" "$MODE" "$SPLIT" "$SPLIT_FILE" "$GT_TOOLS" "$GT_CATS" <<'PY'
import json, sys, collections, os

gt_path, keur_path, out_path, mode = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
split, split_file = sys.argv[5], sys.argv[6]
gt_tools = set(t for t in sys.argv[7].split(",") if t) if len(sys.argv) > 7 else set()
gt_cats  = set(c for c in sys.argv[8].split(",") if c) if len(sys.argv) > 8 else set()

# repo -> split assignment (unlisted = train)
assign = {}
if os.path.exists(split_file):
    for line in open(split_file):
        parts = line.split()
        if len(parts) >= 2: assign[parts[0]] = parts[1]
def in_split(repo):
    if split == "all": return True
    return assign.get(repo, "train") == split

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
    f=json.loads(line)
    if not in_split(f.get("repo","")): continue   # only this split's repos
    if gt_tools and f.get("tool","") not in gt_tools: continue  # proven-tool filter
    if gt_cats and f.get("category","") not in gt_cats: continue  # category scope
    gt[key(f)].append(f)

keur = collections.defaultdict(list)
for line in open(keur_path):
    line=line.strip()
    if not line: continue
    f=json.loads(line)
    if gt_cats and f.get("category","") not in gt_cats: continue  # same scope as GT
    keur[key(f)].append(f)

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
