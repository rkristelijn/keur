#!/usr/bin/env bash
# eval/groundtruth-cpm.sh — use cpm's proven rule corpus as a ground-truth
# source, exactly like groundtruth-awssec.sh does for the AWS Security Agent.
#
# cpm (https://github.com/rkristelijn/cpm) ships ~186 rules across security,
# quality, a11y, secrets, supply-chain, etc. Its .rule format is keur-compatible
# (keur's engine derives from cpm's), so we do not need to parse cpm's output —
# we run keur's OWN engine with cpm's rules over the cpm-eval corpus and emit the
# findings as ground truth. keur then learns to reproduce that coverage with its
# own (smaller) rule set. "adopt another proven tool's shift-left", one corpus at
# a time.
#
# Usage:
#   eval/groundtruth-cpm.sh [<repo-name>]
#     <repo-name> tags the findings (default: cpm-eval); clone/symlink the
#     scanned repo into eval/corpus/repos/<repo-name> so the join works.
#
# Env:
#   CPM_RULES   cpm rules dir (default ~/git/hub/cpm/rules)
#   CPM_EVAL    corpus to scan (default ~/git/hub/cpm-eval)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
NATIVE="$KEUR_HOME/bin/keur-rules"
CPM_RULES="${CPM_RULES:-$HOME/git/hub/cpm/rules}"
CPM_EVAL="${CPM_EVAL:-$HOME/git/hub/cpm-eval}"
REPO="${1:-cpm-eval}"

[[ -x "$NATIVE" ]]     || { echo "cpm-gt: keur-rules not built (cd src/rules && make)" >&2; exit 1; }
[[ -d "$CPM_RULES" ]]  || { echo "cpm-gt: cpm rules dir not found: $CPM_RULES" >&2; exit 1; }
[[ -d "$CPM_EVAL" ]]   || { echo "cpm-gt: cpm-eval corpus not found: $CPM_EVAL" >&2; exit 1; }

# Run keur's engine with ONLY cpm's rules (empty KEUR_HOME so keur's own rules
# are not mixed in) over the cpm-eval corpus. Normalise each finding to the keur
# ground-truth shape, tagged with the repo name so it joins the corpus repo of
# the same name. cpm categories are mapped onto keur's coarse category space so
# measure.sh's (repo,category) join lines up with what keur's own rules produce.
#
# NOTE: the normaliser is passed via `python3 -c` (not a heredoc): a heredoc
# would hijack stdin and the piped findings would be lost (that exact bug ate
# every finding during development).
GT_PY='
import json, sys, os
repo, corpus_root = sys.argv[1], sys.argv[2]
CAT_MAP = {
    "security": "sast", "antipattern": "sast", "best-practice": "sast",
    "performance": "sast", "build": "sast", "supply-chain": "deps",
    "secrets": "secret", "a11y": "a11y", "accessibility": "a11y",
    "inclusivity": "inclusivity", "style": "quality", "quality": "quality",
}
seen = set()
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        f = json.loads(line)
    except Exception:
        continue
    rel = f.get("file", "")
    if rel.startswith(corpus_root):
        rel = os.path.relpath(rel, corpus_root)
    rel = rel.lstrip("./")
    cat = CAT_MAP.get(f.get("category", ""), f.get("category", "sast"))
    key = (rel, cat)
    if key in seen:
        continue
    seen.add(key)
    print(json.dumps({
        "source": "groundtruth", "tool": "cpm",
        "rule": f.get("rule", "cpm"), "category": cat,
        "file": rel, "line": f.get("line", 0), "repo": repo,
    }))
'
KEUR_HOME="" KEUR_RULES_DIRS="$CPM_RULES" "$NATIVE" --dir "$CPM_EVAL" 2>/dev/null \
  | python3 -c "$GT_PY" "$REPO" "$CPM_EVAL"
