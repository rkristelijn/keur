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
if [[ "${1:-}" = "--tool" ]]; then TOOL="${2:-all}"; shift 2; fi
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

# --- osv-scanner: dependency vulnerabilities (OSV database) ------------------
run_osv() {
    have osv-scanner || { echo "groundtruth: osv-scanner absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    # recursive scan of the tree; JSON to a temp file (heredoc owns stdin).
    osv-scanner scan --recursive --format json "$TARGET" > "$out" 2>/dev/null || true
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
for res in data.get("results", []) or []:
    src = rel((res.get("source") or {}).get("path", ""))
    for pkg in res.get("packages", []) or []:
        for v in pkg.get("vulnerabilities", []) or []:
            print(json.dumps({
                "source": "groundtruth", "tool": "osv-scanner",
                "rule": v.get("id", "vuln"), "category": "supply-chain",
                "file": src, "line": 0,
            }))
PY
    rm -f "$out"
}

# --- grype: dependency vulnerabilities (SBOM-based) --------------------------
run_grype() {
    have grype || { echo "groundtruth: grype absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    grype "dir:$TARGET" -o json > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {}
root = sys.argv[2].rstrip("/")
for m in data.get("matches", []) or []:
    vuln = m.get("vulnerability", {}) or {}
    locs = ((m.get("artifact") or {}).get("locations") or [])
    path = locs[0].get("path", "") if locs else ""
    if os.path.isabs(path):
        path = os.path.relpath(path, root)
    print(json.dumps({
        "source": "groundtruth", "tool": "grype",
        "rule": vuln.get("id", "vuln"), "category": "supply-chain",
        "file": path.lstrip("/"), "line": 0,
    }))
PY
    rm -f "$out"
}

# --- bandit: Python SAST (code patterns) -------------------------------------
run_bandit() {
    have bandit || { echo "groundtruth: bandit absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    bandit -r "$TARGET" -f json -q > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {}
root = sys.argv[2].rstrip("/")
def categorise(tid):
    t = (tid or "").lower()
    if "sql" in t: return "sqli"
    if "subprocess" in t or "shell" in t or "exec" in t: return "cmdi"
    if "hardcoded" in t or "password" in t: return "secret"
    return "sast"
for r in data.get("results", []) or []:
    path = r.get("filename", "")
    rel = os.path.relpath(path, root) if os.path.isabs(path) else path
    tid = r.get("test_id", "bandit")
    print(json.dumps({
        "source": "groundtruth", "tool": "bandit",
        "rule": tid, "category": categorise(r.get("test_name", tid)),
        "file": rel, "line": r.get("line_number", 0) or 0,
    }))
PY
    rm -f "$out"
}

# --- njsscan: Node.js/JS SAST (code patterns) --------------------------------
run_njsscan() {
    have njsscan || { echo "groundtruth: njsscan absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    njsscan --json -o "$out" "$TARGET" >/dev/null 2>&1 || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {}
root = sys.argv[2].rstrip("/")
def categorise(rule):
    r = (rule or "").lower()
    if "sql" in r: return "sqli"
    if "command" in r or "exec" in r: return "cmdi"
    if "secret" in r or "hardcoded" in r: return "secret"
    return "sast"
# njsscan: nodejs_security + template_injection groups -> results[rule].files[]
for group in ("nodejs", "templates"):
    sec = (data.get("nodejs") or {}) if group == "nodejs" else (data.get("templates") or {})
    for rule, body in (sec or {}).items():
        for f in (body.get("files") or []):
            path = f.get("file_path", "")
            rel = os.path.relpath(path, root) if os.path.isabs(path) else path
            ln = (f.get("match_lines") or [0])
            print(json.dumps({
                "source": "groundtruth", "tool": "njsscan",
                "rule": rule, "category": categorise(rule),
                "file": rel, "line": ln[0] if isinstance(ln, list) and ln else 0,
            }))
PY
    rm -f "$out"
}

# --- tfsec: Terraform security (code patterns) -------------------------------
run_tfsec() {
    have tfsec || { echo "groundtruth: tfsec absent, skipped" >&2; return; }
    local out; out="$(mktemp)"
    tfsec "$TARGET" --format json --no-color > "$out" 2>/dev/null || true
    python3 - "$out" "$TARGET" <<'PY'
import json, sys, os
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    data = {}
root = sys.argv[2].rstrip("/")
for r in data.get("results", []) or []:
    loc = r.get("location", {}) or {}
    path = loc.get("filename", "")
    rel = os.path.relpath(path, root) if os.path.isabs(path) else path
    print(json.dumps({
        "source": "groundtruth", "tool": "tfsec",
        "rule": r.get("rule_id", "tfsec"), "category": "iac",
        "file": rel, "line": loc.get("start_line", 0) or 0,
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
  osv)        run_osv ;;
  grype)      run_grype ;;
  bandit)     run_bandit ;;
  njsscan)    run_njsscan ;;
  tfsec)      run_tfsec ;;
  all)        run_gitleaks; run_semgrep; run_trufflehog; run_trivy; run_checkov; run_osv; run_grype; run_bandit; run_njsscan; run_tfsec ;;
  *) echo "unknown tool: $TOOL (use gitleaks|semgrep|trufflehog|trivy|checkov|osv|grype|bandit|njsscan|tfsec|all)" >&2; exit 1 ;;
esac
