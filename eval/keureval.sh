#!/usr/bin/env bash
# eval/keureval.sh — evaluate keur against the keur-eval CONTRACT corpus.
#
# This is the FOURTH loop, the missing bridge. Where measure.sh scores keur
# against proven-tool ground truth (recall-focused: "what do we miss?"),
# keureval.sh scores keur against the hand/LLM-authored keur-eval corpus and its
# KEUR-EXPECT / KEUR-EXPECT-NOT contract — which is where FALSE POSITIVES are
# defined. A negative case says "nothing must fire here"; if keur fires, that is
# a real FP and precision must drop. That is the training signal for FP-avoidance
# that measure.sh cannot give (it only knows FPs as "fired where GT had nothing").
#
# It writes the SAME measure.json shape as measure.sh so optimise.sh can consume
# either source interchangeably (see KEUR_EVAL_SOURCE in optimise.sh).
#
# Scoring (per case, at CATEGORY granularity — the contract rule-IDs live in a
# different namespace than keur's own rule-IDs, so we bridge domain->category):
#   positive case: keur fires a rule of the expected category  -> TP, else FN
#   negative case: keur fires a rule of a forbidden category    -> FP, else TN
# Aggregate -> recall, precision, f1, fn (same keys as measure.json).
#
# Usage:
#   eval/keureval.sh            print metrics + write eval/corpus/measure.json
#   eval/keureval.sh --fn       list the FN clusters (what the optimiser targets)
#
# Env:
#   KEUR_EVAL         keur-eval repo path (default ~/git/hub/keur-eval)
#   KEUR_RULES_DIRS   rule dirs for keur-rules (default keur's own rules)
#   KEUR_SPLIT        train|holdout|all (default all); uses keur-eval class map
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
export KEUR_HOME
KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
INDEX="$KEUR_EVAL/index.json"
OUT="$HERE/corpus/measure.json"
NATIVE="$KEUR_HOME/bin/keur-rules"

[[ -f "$INDEX" ]]  || { echo "no keur-eval index: $INDEX (run build-index.sh in keur-eval)" >&2; exit 1; }
[[ -x "$NATIVE" ]] || { echo "keur-rules not built; run: (cd src/rules && make)" >&2; exit 1; }

MODE="metrics"; [[ "${1:-}" = "--fn" ]] && MODE="fn"
SPLIT="${KEUR_SPLIT:-all}"

# Run keur once over the whole corpus (fast: single binary invocation per case
# is avoided by scanning the whole tree and keying results by absolute path).
# keur-rules emits JSONL with an absolute-or-relative "file" field; we scan with
# --dir so paths are relative to the corpus root, then join on that.
KEUR_FINDINGS="$(mktemp)"; trap 'rm -f "$KEUR_FINDINGS"' EXIT
"$NATIVE" --dir "$KEUR_EVAL" 2>/dev/null > "$KEUR_FINDINGS" || true

python3 - "$INDEX" "$KEUR_FINDINGS" "$OUT" "$MODE" "$SPLIT" "$KEUR_EVAL" <<'PY'
import json, sys, collections, os

index_path, keur_path, out_path, mode, split, corpus_root = sys.argv[1:7]

# --- domain/category bridge -------------------------------------------------
# The contract categories (keur-eval) map onto keur's finding categories. A
# positive "sqli" case is satisfied by ANY keur finding whose category is in the
# mapped set; a negative "secrets-safe" case is violated (FP) by any finding in
# the mapped set. This is category granularity, matching measure.sh's Option A.
CAT_MAP = {
    "sqli":          {"sast", "sqli"},
    "cmdi":          {"sast", "cmdi"},
    "secrets":       {"secret"},
    "secrets-safe":  {"secret"},
    "pii":           {"pii"},
    "pii-safe":      {"pii"},
    "cbi":           {"cbi"},
    "cbi-safe":      {"cbi"},
    "personal-docs": {"pii", "cbi"},
}
def mapped(cat):
    return CAT_MAP.get(cat, {cat})

# --- load index -------------------------------------------------------------
doc = json.load(open(index_path))
cases = doc.get("cases", doc) if isinstance(doc, dict) else doc

# Split: hold out ~1/5 of cases deterministically by path hash so "holdout" is
# stable across runs and the optimiser never trains on it. "all" uses everything.
def in_split(path):
    if split == "all":
        return True
    import zlib
    bucket = zlib.crc32(path.encode()) % 5   # 0..4
    held = (bucket == 0)                      # 20% holdout
    return held if split == "holdout" else (not held)

# --- load keur findings, key by relative path -------------------------------
# keur-rules --dir emits "file" relative to the scanned root. Normalise both to
# a path relative to corpus_root so they join with index "path" entries.
fires = collections.defaultdict(set)   # relpath -> set(categories keur fired)
for line in open(keur_path):
    line = line.strip()
    if not line:
        continue
    try:
        f = json.loads(line)
    except Exception:
        continue
    rel = f.get("file", "")
    # strip a leading corpus_root or "./" if present
    if rel.startswith(corpus_root):
        rel = os.path.relpath(rel, corpus_root)
    rel = rel.lstrip("./")
    fires[rel].add(f.get("category", ""))

# --- score ------------------------------------------------------------------
tp = fn = fp = tn = 0
fn_cases = []   # (category, path) the optimiser should target
fp_cases = []

for c in cases:
    path = c.get("path", "")
    if not in_split(path):
        continue
    cls = c.get("class", "")
    cat = c.get("category", "")
    want = mapped(cat)
    fired_cats = fires.get(path, set())
    fired_want = bool(want & fired_cats)

    if cls == "positive":
        if fired_want:
            tp += 1
        else:
            fn += 1
            fn_cases.append((cat, path))
    else:  # negative: the contract is "KEUR-EXPECT: none" — NOTHING may fire.
        # Any fire (of ANY category) on a negative case is a false positive.
        # This strictness is deliberate: it punishes broad, noisy rules that
        # match unrelated code (e.g. a SQLi rule firing on a secrets look-alike),
        # which is exactly the FP-avoidance signal measure.sh cannot give.
        if fired_cats:
            fp += 1
            # attribute the FP to the categories that actually fired, so the
            # optimiser can see WHICH rule class is noisy on this case.
            fp_cases.append((",".join(sorted(fired_cats)) or cat, path))
        else:
            tn += 1

def safe(a, b):
    return a / b if b else 0.0
recall    = safe(tp, tp + fn)
precision = safe(tp, tp + fp)
f1        = safe(2 * precision * recall, precision + recall)

if mode == "fn":
    by_cat = collections.Counter(cat for cat, _ in fn_cases)
    print("FN clusters (positive cases keur misses), by category:")
    for cat, n in by_cat.most_common():
        print(f"  {n:4d}  {cat}")
    print("\nsample missed findings:")
    for cat, path in fn_cases[:10]:
        # emit in the same 3-column shape measure.sh --fn uses so sample_fn in
        # optimise.sh parses it identically: tool  category  repo/file
        print(f"  keureval  {cat:8s}  {path}")
else:
    result = {
        "tp": tp, "fn": fn, "fp": fp, "tn": tn,
        "recall": round(recall, 4), "precision": round(precision, 4), "f1": round(f1, 4),
        "fn_by_category": dict(collections.Counter(cat for cat, _ in fn_cases)),
        "fp_by_category": dict(collections.Counter(cat for cat, _ in fp_cases)),
    }
    json.dump(result, open(out_path, "w"), indent=2)
    print(f"keur vs keur-eval contract:  TP={tp} FN={fn} FP={fp} TN={tn}")
    print(f"  recall    {recall:.3f}  (positive cases detected)")
    print(f"  precision {precision:.3f}  (negative cases NOT falsely flagged)")
    print(f"  f1        {f1:.3f}")
    if result["fp_by_category"]:
        print(f"  FALSE POSITIVES by category: {result['fp_by_category']}")
    if result["fn_by_category"]:
        print(f"  missed by category: {result['fn_by_category']}")
PY
