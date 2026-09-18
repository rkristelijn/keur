#!/usr/bin/env bash
# eval/buildgt.sh — build the ground-truth cache over the whole corpus.
#
# Runs the proven-tool adapters (groundtruth.sh) over every cloned repo and
# writes ONE append-only JSONL at eval/corpus/groundtruth.jsonl. Each line is a
# normalised finding tagged with its repo, so keur can later be measured
# against it. Resource-safe: preflight before each repo, tools already capped.
#
# The ground truth is source-agnostic: today it is gitleaks + semgrep; tomorrow
# a Sonar / CodeRabbit / Dependabot adapter writes the same JSON shape here and
# the loop does not change.
#
# Usage:
#   eval/buildgt.sh [--tool T]     build/refresh the cache (default: all tools)
#   eval/buildgt.sh --stats        summarise the existing cache
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
# Cache + ground-truth output default in-repo but follow KEUR_CORPUS_DIR / KEUR_GT
# so an alternate corpus (e.g. ~/git/hub/keur-eval-3) is fully self-contained.
CACHE="${KEUR_CORPUS_DIR:-$HERE/corpus/repos}"
GT="${KEUR_GT:-$HERE/corpus/groundtruth.jsonl}"

if [ "${1:-}" = "--stats" ]; then
    [ -f "$GT" ] || { echo "no ground truth yet: $GT"; exit 0; }
    python3 - "$GT" <<'PY'
import json, sys, collections
tools = collections.Counter(); cats = collections.Counter(); repos = collections.Counter()
n = 0
for line in open(sys.argv[1]):
    line = line.strip()
    if not line: continue
    try: f = json.loads(line)
    except Exception: continue
    n += 1
    tools[f.get("tool","?")] += 1
    cats[f.get("category","?")] += 1
    repos[f.get("repo","?")] += 1
print(f"ground truth: {n} findings across {len(repos)} repo(s)")
print("  by tool:    ", dict(tools))
print("  by category:", dict(cats))
PY
    exit 0
fi

TOOL_ARG=()
if [ "${1:-}" = "--tool" ]; then TOOL_ARG=(--tool "${2:-all}"); shift 2; fi

[ -d "$CACHE" ] || { echo "no corpus; run: eval/corpus.sh clone" >&2; exit 1; }

mkdir -p "$(dirname "$GT")"
: > "$GT"   # fresh cache
total=0
for repo in "$CACHE"/*/; do
    [ -d "$repo" ] || continue
    name="$(basename "$repo")"
    # preflight per repo so a long build cannot run the machine dry.
    if ! "$HERE/preflight.sh" "$KEUR_HOME" >/dev/null 2>&1; then
        echo "buildgt: resources low, stopping after $total findings" >&2
        break
    fi
    echo "  scan  $name" >&2
    n=0
    while IFS= read -r finding; do
        [ -z "$finding" ] && continue
        # tag with the repo so measurement can join keur findings by repo+file+line
        echo "$finding" | python3 -c "import json,sys; f=json.load(sys.stdin); f['repo']='$name'; print(json.dumps(f))" >> "$GT"
        n=$((n+1))
    done < <("$HERE/groundtruth.sh" "${TOOL_ARG[@]}" "$repo" 2>/dev/null)
    total=$((total+n))
    echo "        $n finding(s)" >&2
done

# --- deduplicate / combine across tools -------------------------------------
# Multiple scanners often flag the SAME issue (e.g. gitleaks + trufflehog both
# find one private key). Collapse findings that share (repo, file, category)
# into a SINGLE record, keeping the list of tools that saw it plus a seen_by
# count. This gives clean statistics (no double-counting) and a confidence
# signal: a finding confirmed by more tools is more trustworthy. Line numbers
# differ between tools for the same issue, so they are NOT part of the key; the
# earliest reported line is kept.
if [ -s "$GT" ]; then
    dedup="$(mktemp)"
    python3 - "$GT" > "$dedup" <<'PY'
import json, sys, collections
combined = collections.OrderedDict()
for line in open(sys.argv[1]):
    line = line.strip()
    if not line:
        continue
    try:
        f = json.loads(line)
    except Exception:
        continue
    k = (f.get("repo",""), f.get("file",""), f.get("category",""))
    if k not in combined:
        f["tools"] = [f.get("tool","?")]
        f["seen_by"] = 1
        combined[k] = f
    else:
        c = combined[k]
        t = f.get("tool","?")
        if t not in c["tools"]:
            c["tools"].append(t)
            c["seen_by"] = len(c["tools"])
        # keep the earliest line reported for the same issue
        if f.get("line", 0) and (not c.get("line") or f["line"] < c["line"]):
            c["line"] = f["line"]
for f in combined.values():
    print(json.dumps(f))
PY
    before="$total"
    total="$(wc -l < "$dedup" | tr -d ' ')"
    mv "$dedup" "$GT"
    echo "dedup: $before raw -> $total combined finding(s)" >&2
fi

echo "ground truth: $total finding(s) -> $GT" >&2
