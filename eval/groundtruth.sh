#!/usr/bin/env bash
# eval/groundtruth.sh — run PROVEN scanners over a path and normalise their
# output to KEUR Finding JSON lines. This is "libmode": keur wrapping external
# tools rather than its own rule engine.
#
# The union of these findings is the GROUND TRUTH the rule engine is trained to
# approach. Each finding is normalised to one JSON object per line:
#   {"source","tool","rule","category","file","line"}
#
# Usage:
#   eval/groundtruth.sh <path>            all available tools -> findings on stdout
#   eval/groundtruth.sh --tool gitleaks <path>
#
# Missing tools are skipped (a warning to stderr), never fatal.
set -uo pipefail

TOOL="all"
if [ "${1:-}" = "--tool" ]; then TOOL="${2:-all}"; shift 2; fi
TARGET="${1:?path required}"

have() { command -v "$1" >/dev/null 2>&1; }

# --- gitleaks: secrets -------------------------------------------------------
run_gitleaks() {
    have gitleaks || { echo "groundtruth: gitleaks absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    # --no-git: scan the working tree (shallow clones have little history).
    gitleaks detect --source "$TARGET" --no-git -f json -r "$out" >/dev/null 2>&1 || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = []
root = sys.argv[2].rstrip("/")
for f in data:
    rel = f.get("File", "")
    # gitleaks paths are relative to --source already
    print(json.dumps({
        "source": "groundtruth", "tool": "gitleaks",
        "rule": f.get("RuleID", "secret"), "category": "secret",
        "file": rel, "line": f.get("StartLine", 0),
    }))
PY
    rm -f "$out"
}

# --- semgrep: SAST (code patterns) ------------------------------------------
run_semgrep() {
    have semgrep || { echo "groundtruth: semgrep absent, skipped" >&2; return; }
    # Resource guards so a big repo cannot exhaust RAM or hang:
    #   --jobs 1        one worker (predictable memory, composes with Ollama)
    #   --max-memory    hard cap in MB per rule (override KEUR_SEMGREP_MAX_MB)
    #   --timeout       per-rule-per-file seconds (override KEUR_SEMGREP_TIMEOUT)
    #   --max-target-bytes  skip huge files
    local maxmb="${KEUR_SEMGREP_MAX_MB:-2048}"
    local tmo="${KEUR_SEMGREP_TIMEOUT:-30}"
    semgrep --config=p/default --json --quiet \
            --jobs 1 --max-memory "$maxmb" --timeout "$tmo" \
            --max-target-bytes 1000000 \
            "$TARGET" 2>/dev/null | python3 - "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(sys.stdin)
except Exception:
    data = {"results": []}
root = sys.argv[1].rstrip("/")
def categorise(check_id):
    c = check_id.lower()
    if "sql" in c: return "sqli"
    if any(k in c for k in ("command", "os-system", "subprocess", "exec")): return "cmdi"
    if "secret" in c or "hardcoded" in c: return "secret"
    return "sast"
for r in data.get("results", []):
    path = r.get("path", "")
    rel = os.path.relpath(path, root) if os.path.isabs(path) else path
    cid = r.get("check_id", "semgrep")
    print(json.dumps({
        "source": "groundtruth", "tool": "semgrep",
        "rule": cid, "category": categorise(cid),
        "file": rel, "line": r.get("start", {}).get("line", 0),
    }))
PY
}

case "$TOOL" in
  gitleaks) run_gitleaks ;;
  semgrep)  run_semgrep ;;
  all)      run_gitleaks; run_semgrep ;;
  *) echo "unknown tool: $TOOL (use gitleaks|semgrep|all)" >&2; exit 1 ;;
esac
