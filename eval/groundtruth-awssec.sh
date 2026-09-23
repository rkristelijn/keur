#!/usr/bin/env bash
# eval/groundtruth-awssec.sh — parse an AWS Security Agent code-review report
# (PDF) into normalised KEUR Finding JSON, same shape as the gitleaks/semgrep
# adapters. This makes a real third-party reviewer a ground-truth source: the
# "adopt the whole internet's shift-left" direction, one adapter at a time.
#
# The report carries exact file:line references (e.g. api/Controller.java:160
# and full /tmp/opt/source_code/.../src/... paths). We extract them, normalise
# to repo-relative paths, and emit one finding per (file,line-range).
#
# Usage:
#   eval/groundtruth-awssec.sh <report.pdf> <repo-name>
#     <repo-name> tags the findings so they join with the corpus repo of the
#     same name (clone the reviewed repo into eval/corpus/repos/<repo-name>).
#
# Category mapping is heuristic from the finding text (secret/sqli/cmdi/authz/
# sast). Everything the agent flags is at least "sast".
set -uo pipefail

REPORT="${1:?report.pdf required}"
REPO="${2:?repo-name required (matches eval/corpus/repos/<name>)}"

# Optional prefix to prepend to short "api/Foo.java:NN" refs so they resolve to
# the repo layout. Report-/project-specific, so it is passed in, not baked in.
# Example for a Java service:  KEUR_ASA_PREFIX="src/main/java/com/acme/svc/"
PREFIX="${KEUR_ASA_PREFIX:-}"

command -v pdftotext >/dev/null 2>&1 || { echo "awssec: pdftotext required" >&2; exit 1; }
[[ -f "$REPORT" ]] || { echo "awssec: report not found: $REPORT" >&2; exit 1; }

pdftotext -layout "$REPORT" - 2>/dev/null > "$(dirname "$0")/.awssec.tmp" || true
TXT="$(dirname "$0")/.awssec.tmp"
trap 'rm -f "$TXT"' EXIT
python3 - "$REPO" "$TXT" "$PREFIX" <<'PY'
import sys, re, json, os

repo   = sys.argv[1]
prefix = sys.argv[3] if len(sys.argv) > 3 else ""
text = open(sys.argv[2], encoding="utf-8", errors="ignore").read()

def categorise(ctx):
    c = ctx.lower()
    if "sql" in c: return "sqli"
    if any(k in c for k in ("command inject", "shell", "os.system", "exec(")): return "cmdi"
    if any(k in c for k in ("secret", "credential", "password", "hardcoded", "token", "api key")): return "secret"
    if any(k in c for k in ("privileged", "access control", "authz", "authoriz", "audit trail", "admin role")): return "authz"
    return "sast"

# File:line references appear in two forms:
#   short:  api/Controller.java:160-218   (relative to src/main/java/.../erp/)
#   full:   /tmp/opt/source_code/.../src/main/java/com/.../Controller.java (L160-L218)
# We capture both and normalise to a repo-relative path when possible.
findings = {}   # (file,line) -> category, dedup

# full paths with (Lxxx) ranges
for m in re.finditer(r'(/tmp/opt/source_code/\S*?/(src/\S+?\.(?:java|properties|xml|ts|js|py)))\s*\(L(\d+)', text):
    rel = m.group(2)
    line = int(m.group(3))
    # a little surrounding context for categorisation
    start = max(0, m.start()-200)
    ctx = text[start:m.end()+80]
    findings[(rel, line)] = categorise(ctx)

# short refs like api/Controller.java:160  -> map under the java package root
for m in re.finditer(r'\b((?:api|config|data|service|util|domain)/[\w/]+\.(?:java|properties|xml)):(\d+)', text):
    short = m.group(1); line = int(m.group(2))
    rel = (prefix + short) if prefix else short
    ctx = text[max(0,m.start()-200):m.end()+80]
    findings.setdefault((rel, line), categorise(ctx))

for (rel, line), cat in sorted(findings.items()):
    print(json.dumps({
        "source": "groundtruth", "tool": "aws-security-agent",
        "rule": "ASA-" + cat.upper(), "category": cat,
        "file": rel, "line": line, "repo": repo,
    }))
PY
