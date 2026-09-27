#!/usr/bin/env bash
# eval/benchmark.sh — head-to-head benchmark of every detector against the
# AUTHORED ground truth (the repos' own vuln markers). Measures, per tool:
#   - TP / FP / recall  (via eval/benchmark-score.py, matched at author loci)
#   - wall-clock time    (how fast — the pre-commit-hook currency)
#
# Detectors:
#   keur                    the RE2 rule engine (bin/keur scan)
#   the proven tools        via eval/groundtruth.sh --tool <t> (semgrep, bandit,
#                           njsscan, gitleaks, trufflehog, trivy, grype, checkov,
#                           tfsec, hadolint, psalm) — SAST + secrets + SCA + IaC
#   sonar (optional)        if a SonarQube server is up (eval/sonar.sh up); skipped otherwise
#
# Findings per detector are written to eval/.run/bench/<tool>.jsonl, timings to
# eval/.run/bench/timings.tsv, then scored by benchmark-score.py.
#
# Usage:
#   eval/benchmark.sh              run all available detectors + score
#   eval/benchmark.sh --tools "semgrep bandit"   only these + keur
#   eval/benchmark.sh --score-only               re-score existing run (no re-run)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
CORPUS="$HERE/corpus/repos"
OUT="$HERE/.run/bench"
TIMINGS="$OUT/timings.tsv"
RULES="$KEUR_HOME/rules/security:$KEUR_HOME/rules/secrets:$KEUR_HOME/rules/supply-chain:$KEUR_HOME/rules/quality:$KEUR_HOME/rules/inclusivity:$KEUR_HOME/rules/composite"

ALL_TOOLS="semgrep bandit njsscan psalm gitleaks trufflehog trivy grype checkov tfsec hadolint"
TOOLS="$ALL_TOOLS"
SCORE_ONLY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tools)      TOOLS="$2"; shift 2 ;;
    --score-only) SCORE_ONLY=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done

have() { command -v "$1" >/dev/null 2>&1; }
mkdir -p "$OUT"

# --- time a command in seconds (python perf_counter — macOS-safe) -----------
timed() {  # timed <label> <outfile> -- <cmd...>
  local label="$1" outfile="$2"; shift 2; [[ "$1" == "--" ]] && shift
  local t0 t1
  t0=$(python3 -c 'import time;print(time.perf_counter())')
  "$@" > "$outfile" 2>/dev/null || true
  t1=$(python3 -c 'import time;print(time.perf_counter())')
  local n; n=$(wc -l < "$outfile" | tr -d ' ')
  python3 -c "print(f'{\"$label\"}\t{$t1-$t0:.2f}\t{$n}')"
}

if [[ "$SCORE_ONLY" != "1" ]]; then
  : > "$TIMINGS"
  echo -e "tool\tseconds\tfindings" >> "$TIMINGS"

  # --- keur: scan every repo, sum time --------------------------------------
  echo "benchmark: keur ..." >&2
  keur_out="$OUT/keur.jsonl"; : > "$keur_out"
  t0=$(python3 -c 'import time;print(time.perf_counter())')
  for d in "$CORPUS"/*/; do
    repo="$(basename "$d")"
    KEUR_RULES_DIRS="$RULES" "$KEUR_HOME/bin/keur" scan "$d" 2>/dev/null | \
      python3 -c "import json,sys
repo=sys.argv[1]
for l in sys.stdin:
    l=l.strip()
    if not l: continue
    try: f=json.loads(l)
    except: continue
    fp=f.get('file','')
    print(json.dumps({'tool':'keur','repo':repo,'file':fp,'line':f.get('line',0),'category':f.get('category','')}))" "$repo" >> "$keur_out"
  done
  t1=$(python3 -c 'import time;print(time.perf_counter())')
  kn=$(wc -l < "$keur_out" | tr -d ' ')
  python3 -c "print(f'keur\t{$t1-$t0:.2f}\t$kn')" >> "$TIMINGS"

  # --- each proven tool: scan every repo, sum time --------------------------
  for tool in $TOOLS; do
    have "$tool" || { echo "benchmark: $tool absent, skipped" >&2; continue; }
    echo "benchmark: $tool ..." >&2
    tout="$OUT/$tool.jsonl"; : > "$tout"
    t0=$(python3 -c 'import time;print(time.perf_counter())')
    for d in "$CORPUS"/*/; do
      repo="$(basename "$d")"
      "$HERE/groundtruth.sh" --tool "$tool" "$d" 2>/dev/null | \
        python3 -c "import json,sys
repo=sys.argv[1]
for l in sys.stdin:
    l=l.strip()
    if not l: continue
    try: f=json.loads(l)
    except: continue
    fp=f.get('file','')
    if '/repos/' in fp: fp=fp.split('/repos/',1)[1].split('/',1)[-1]
    print(json.dumps({'tool':'$tool','repo':repo,'file':fp,'line':f.get('line',0),'category':f.get('category','')}))" "$repo" >> "$tout"
    done
    t1=$(python3 -c 'import time;print(time.perf_counter())')
    tn=$(wc -l < "$tout" | tr -d ' ')
    python3 -c "print(f'$tool\t{$t1-$t0:.2f}\t$tn')" >> "$TIMINGS"
  done
fi

# --- score -----------------------------------------------------------------
python3 "$HERE/benchmark-score.py" "$OUT" "$HERE/corpus/authored-groundtruth.jsonl" "$TIMINGS"
