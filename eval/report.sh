#!/usr/bin/env bash
# eval/report.sh — SAST + posture/hardening findings over the corpus, in one
# unified report (console + JSON + CSV + JUnit XML).
#
# SCOPE (deliberately narrow, per the product focus):
#   - SAST: code-level weaknesses (injection, unsafe calls, crypto misuse,
#     framework-specific hardening gaps).
#   - POSTURE/HARDENING: unpinned versions (deps AND runtime/base images),
#     missing hardening where it belongs (Dockerfile USER, K8s runAsNonRoot,
#     TLS/transport config, framework security config).
#   NOT SCA (no dependency-CVE scanning), NOT secret entropy scanning.
#
# SOURCES (established tools, source-agnostic — each normalises to the same
# Finding JSON shape; more can be added the same way):
#   - cpm      : the richest SAST + framework + hardening rule corpus we have
#                (273 security + framework dirs express/flask/django/spring/...
#                + docker/k8s/iac/transport). Run via groundtruth-cpm.sh — no
#                adapter needed, its .rule format is keur-native.
#   - semgrep  : independent second opinion (SAST + framework rules) — its
#                agreement raises confidence (seen_by), per keur.toml
#                corroboration-bonus.
#   - trivy    : misconfig scanner — Dockerfile/IaC/K8s hardening (posture).
#   - checkov  : IaC misconfig — hardening (posture).
#
# A finding confirmed by >1 independent tool (seen_by >= 2) is more trustworthy;
# the report tiers findings by that count.
#
# Usage:
#   eval/report.sh                 run all available sources, write all formats
#   eval/report.sh --console       console summary only (no files)
#   eval/report.sh --outdir DIR    where to write report.* (default eval/report)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
export KEUR_HOME
CACHE="$HERE/corpus/repos"

OUTDIR="$HERE/report"
CONSOLE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --console) CONSOLE_ONLY=1; shift ;;
        --outdir)  OUTDIR="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

[ -d "$CACHE" ] || { echo "no corpus; run: eval/corpus.sh clone" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# SAST + posture categories we keep. Everything the tools emit is mapped into
# keur's coarse category space by groundtruth.sh; we drop SCA/deps and raw
# secret-entropy noise so the report stays on-scope.
KEEP_CATS="sast sqli cmdi iac transport"

RAW="$(mktemp)"; trap 'rm -f "$RAW"' EXIT

echo "keur report — SAST + posture/hardening over the corpus" >&2
echo "  sources:" >&2

# --- cpm: primary SAST + framework + hardening corpus -----------------------
# groundtruth-cpm.sh runs keur's engine with cpm's rules over the cpm-eval
# corpus and tags findings repo=cpm-eval. On-scope by construction (cpm's
# security/framework/iac rules), so we keep its sast/iac/transport output.
if [ -x "$HERE/groundtruth-cpm.sh" ] && [ -d "${CPM_RULES:-$HOME/git/hub/cpm/rules}" ]; then
    echo "    cpm      (SAST + framework + hardening)" >&2
    "$HERE/groundtruth-cpm.sh" cpm-eval 2>/dev/null >> "$RAW" || true
else
    echo "    cpm      MISSING (skipped)" >&2
fi

# --- external tools over every cloned repo ----------------------------------
# semgrep (SAST + framework), trivy misconfig (posture), checkov (posture).
# groundtruth.sh already normalises + categorises these; we run per-tool so a
# missing tool is skipped, and tag each finding with its repo.
run_tool_over_corpus() {
    local tool="$1" label="$2"
    have "$tool" || { echo "    $tool MISSING (skipped)" >&2; return; }
    echo "    $label" >&2
    for repo in "$CACHE"/*/; do
        [ -d "$repo" ] || continue
        local name; name="$(basename "$repo")"
        "$HERE/preflight.sh" "$KEUR_HOME" >/dev/null 2>&1 || {
            echo "    (resources low, stopping $tool early)" >&2; break; }
        "$HERE/groundtruth.sh" --tool "$tool" "$repo" 2>/dev/null | \
          python3 -c "import json,sys
for line in sys.stdin:
    line=line.strip()
    if not line: continue
    try: f=json.loads(line)
    except Exception: continue
    f['repo']='$name'
    print(json.dumps(f))" >> "$RAW"
    done
}
run_tool_over_corpus semgrep "semgrep  (SAST + framework, second opinion)"
run_tool_over_corpus trivy   "trivy    (misconfig / posture)"
run_tool_over_corpus checkov "checkov  (IaC misconfig / posture)"

# --- normalise: filter to scope, dedup with seen_by, then emit all formats --
python3 - "$RAW" "$KEEP_CATS" "$OUTDIR" "$CONSOLE_ONLY" <<'PY'
import json, sys, os, csv, collections, datetime, html

raw_path, keep_cats, outdir, console_only = sys.argv[1:5]
keep = set(keep_cats.split())
console_only = console_only == "1"

# 1. read + filter to SAST/posture scope
rows = []
for line in open(raw_path):
    line = line.strip()
    if not line:
        continue
    try:
        f = json.loads(line)
    except Exception:
        continue
    if f.get("category") not in keep:
        continue
    rows.append(f)

# 2. dedup / combine across tools on (repo, file, category); track which tools
#    saw it (seen_by = independent-tool agreement = confidence signal).
combined = collections.OrderedDict()
for f in rows:
    k = (f.get("repo",""), f.get("file",""), f.get("category",""))
    if k not in combined:
        f = dict(f)
        f["tools"] = [f.get("tool","?")]
        f["seen_by"] = 1
        combined[k] = f
    else:
        c = combined[k]
        t = f.get("tool","?")
        if t not in c["tools"]:
            c["tools"].append(t)
            c["seen_by"] = len(c["tools"])
        if f.get("line",0) and (not c.get("line") or f["line"] < c["line"]):
            c["line"] = f["line"]
findings = list(combined.values())

# 3. confidence tier from agreement: >=2 tools = high, 1 = single-source.
def tier(f):
    return "high" if f.get("seen_by",1) >= 2 else "single"
for f in findings:
    f["tier"] = tier(f)

# ---- console summary -------------------------------------------------------
by_cat  = collections.Counter(f["category"] for f in findings)
by_tool = collections.Counter(t for f in findings for t in f["tools"])
by_repo = collections.Counter(f["repo"] for f in findings)
by_tier = collections.Counter(f["tier"] for f in findings)

def bar(n, mx, w=30):
    return "#" * int(round(w * n / mx)) if mx else ""

print()
print("=" * 64)
print(f"  keur SAST + posture report — {len(findings)} findings "
      f"across {len(by_repo)} repo(s)")
print("=" * 64)
print(f"\n  confidence (independent-tool agreement):")
print(f"    high   (>=2 tools agree) : {by_tier.get('high',0)}")
print(f"    single (1 tool)          : {by_tier.get('single',0)}")

mx = max(by_cat.values()) if by_cat else 1
print(f"\n  by category:")
for cat, n in by_cat.most_common():
    print(f"    {cat:12s} {n:5d}  {bar(n, mx)}")

mx = max(by_tool.values()) if by_tool else 1
print(f"\n  by tool (contributions, counts overlap on agreed findings):")
for tool, n in by_tool.most_common():
    print(f"    {tool:12s} {n:5d}  {bar(n, mx)}")

mx = max(by_repo.values()) if by_repo else 1
print(f"\n  top repos:")
for repo, n in by_repo.most_common(12):
    print(f"    {repo:42s} {n:5d}  {bar(n, mx, 18)}")
print()

if console_only:
    sys.exit(0)

# ---- file outputs ----------------------------------------------------------
os.makedirs(outdir, exist_ok=True)
stamp = datetime.datetime.now().isoformat(timespec="seconds")

# JSON: full findings + summary (machine-readable, tool integrations)
json_path = os.path.join(outdir, "report.json")
with open(json_path, "w") as fh:
    json.dump({
        "generated": stamp,
        "scope": "sast+posture",
        "summary": {
            "total": len(findings),
            "repos": len(by_repo),
            "by_category": dict(by_cat),
            "by_tool": dict(by_tool),
            "by_tier": dict(by_tier),
        },
        "findings": findings,
    }, fh, indent=2)

# CSV: one row per finding (spreadsheets, quick triage)
csv_path = os.path.join(outdir, "report.csv")
with open(csv_path, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["repo","file","line","category","tools","seen_by","tier","rule"])
    for f in findings:
        w.writerow([f.get("repo",""), f.get("file",""), f.get("line",0),
                    f.get("category",""), ";".join(f.get("tools",[])),
                    f.get("seen_by",1), f.get("tier",""), f.get("rule","")])

# JUnit XML: findings as failing testcases, grouped by repo (CI dashboards)
junit_path = os.path.join(outdir, "report.junit.xml")
by_repo_findings = collections.defaultdict(list)
for f in findings:
    by_repo_findings[f.get("repo","")].append(f)
def esc(s): return html.escape(str(s), quote=True)
with open(junit_path, "w") as fh:
    fh.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    fh.write(f'<testsuites name="keur-sast-posture" tests="{len(findings)}" '
             f'failures="{len(findings)}" timestamp="{esc(stamp)}">\n')
    for repo, fs in sorted(by_repo_findings.items()):
        fh.write(f'  <testsuite name="{esc(repo)}" tests="{len(fs)}" '
                 f'failures="{len(fs)}">\n')
        for f in fs:
            tname = f"{f.get('file','?')}:{f.get('line',0)} [{f.get('category','')}]"
            msg = (f"{f.get('category','')} finding "
                   f"(seen_by {f.get('seen_by',1)}: {','.join(f.get('tools',[]))}) "
                   f"rule={f.get('rule','')}")
            fh.write(f'    <testcase classname="{esc(repo)}" name="{esc(tname)}">\n')
            fh.write(f'      <failure type="{esc(f.get("category",""))}" '
                     f'message="{esc(msg)}"/>\n')
            fh.write(f'    </testcase>\n')
        fh.write('  </testsuite>\n')
    fh.write('</testsuites>\n')

print(f"  written:")
print(f"    {json_path}")
print(f"    {csv_path}")
print(f"    {junit_path}")
print()
PY
