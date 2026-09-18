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
    # Write semgrep JSON to a temp file and pass it as an ARG. We must not pipe
    # it to `python3 - <<'PY'`: the heredoc already claims stdin, so a piped
    # payload is discarded (silent 0 findings). Read from a file, like gitleaks.
    local sg; sg="$(mktemp)"
    semgrep --config=p/default --json --quiet \
            --jobs 1 --max-memory "$maxmb" --timeout "$tmo" \
            --max-target-bytes 1000000 \
            "$TARGET" > "$sg" 2>/dev/null || true
    python3 - "$sg" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {"results": []}
root = sys.argv[2].rstrip("/")
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
    rm -f "$sg"
}

# --- trufflehog: secrets (verified detection) -------------------------------
run_trufflehog() {
    have trufflehog || { echo "groundtruth: trufflehog absent, skipped" >&2; return; }
    # filesystem scan, JSONL on stdout (one finding per line). --no-verification
    # keeps it offline/deterministic (no live API calls to validate a secret),
    # matching gitleaks/semgrep which also do not touch the network here.
    local out; out="$(mktemp)"
    trufflehog filesystem "$TARGET" --json --no-verification > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
root = sys.argv[2].rstrip("/")
# trufflehog emits JSONL: one object per line, not a single array.
for line in open(sys.argv[1]):
    line = line.strip()
    if not line:
        continue
    try:
        f = json.loads(line)
    except Exception:
        continue
    fsm = f.get("SourceMetadata", {}).get("Data", {}).get("Filesystem", {})
    path = fsm.get("file", "")
    if not path:
        continue   # skip non-finding progress/log lines
    rel = os.path.relpath(path, root) if os.path.isabs(path) else path
    # trufflehog paths are already relative to the scan root; strip a leading
    # "root/" if present so it matches gitleaks/semgrep relative paths.
    prefix = root + "/"
    if rel.startswith(prefix):
        rel = rel[len(prefix):]
    print(json.dumps({
        "source": "groundtruth", "tool": "trufflehog",
        "rule": f.get("DetectorName", "secret"), "category": "secret",
        "file": rel, "line": fsm.get("line", 0),
    }))
PY
    rm -f "$out"
}

# --- trivy: IaC misconfig + secrets ------------------------------------------
run_trivy() {
    have trivy || { echo "groundtruth: trivy absent, skipped" >&2; return; }
    # fs scan, misconfig + secret scanners (skip vuln: needs lockfile resolution
    # and network). Quiet, JSON to a temp file (heredoc owns stdin).
    local out; out="$(mktemp)"
    trivy fs --scanners misconfig,secret --format json --quiet \
          "$TARGET" > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {}
root = sys.argv[2].rstrip("/")
def rel(p):
    p = p or ""
    return os.path.relpath(p, root) if os.path.isabs(p) else p
for r in data.get("Results", []) or []:
    tgt = r.get("Target", "")
    for m in (r.get("Misconfigurations") or []):
        line = (m.get("CauseMetadata") or {}).get("StartLine", 0) or 0
        print(json.dumps({
            "source": "groundtruth", "tool": "trivy",
            "rule": m.get("ID", "misconfig"), "category": "iac",
            "file": rel(tgt), "line": line,
        }))
    for s in (r.get("Secrets") or []):
        print(json.dumps({
            "source": "groundtruth", "tool": "trivy",
            "rule": s.get("RuleID", "secret"), "category": "secret",
            "file": rel(tgt), "line": s.get("StartLine", 0) or 0,
        }))
PY
    rm -f "$out"
}

# --- checkov: IaC misconfig (Terraform/k8s/CloudFormation/...) ---------------
run_checkov() {
    have checkov || { echo "groundtruth: checkov absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    checkov -d "$TARGET" --output json --compact --quiet > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = []
# checkov emits a single object or a list of per-framework blocks.
blocks = data if isinstance(data, list) else [data]
for b in blocks:
    if not isinstance(b, dict):
        continue
    for c in (b.get("results", {}) or {}).get("failed_checks", []) or []:
        rng = c.get("file_line_range") or [0]
        # file_path is already repo-relative (leading slash); strip it.
        fp = (c.get("file_path") or "").lstrip("/")
        print(json.dumps({
            "source": "groundtruth", "tool": "checkov",
            "rule": c.get("check_id", "misconfig"), "category": "iac",
            "file": fp, "line": rng[0] if rng else 0,
        }))
PY
    rm -f "$out"
}

case "$TOOL" in
  gitleaks)   run_gitleaks ;;
  semgrep)    run_semgrep ;;
  trufflehog) run_trufflehog ;;
  trivy)      run_trivy ;;
  checkov)    run_checkov ;;
  all)        run_gitleaks; run_semgrep; run_trufflehog; run_trivy; run_checkov ;;
  *) echo "unknown tool: $TOOL (use gitleaks|semgrep|trufflehog|trivy|checkov|all)" >&2; exit 1 ;;
esac
