#!/usr/bin/env bash
set -uo pipefail

# knowledge-ghsa.sh — knowledge adapter #3: GitHub Security Advisories (GHSA).
#
# Reads the GitHub Advisory Database via the official public REST API
# (https://api.github.com/advisories). The database is CC-BY-4.0 and the API is
# documented and rate-limited — no scraping. We take the *class of weakness*
# (summary + CWE + ecosystem) as inspiration only; the generator writes fresh
# fictional code, never the vulnerable code from a referenced project.
#
# Auth is optional: set GITHUB_TOKEN to raise the rate limit. Without it the
# unauthenticated limit (60/h) still allows a small pull.
#
# Usage:
#   knowledge-ghsa.sh [--out FILE] [--per-page N] [--severity S]
#     --per-page  advisories to fetch (default 30, max 100 per API page)
#     --severity  filter: low|medium|high|critical (optional)
#   knowledge-ghsa.sh --stats [--out FILE]

HERE="$(cd "$(dirname "$0")" && pwd -P)"
OUT="$HERE/knowledge/ghsa.jsonl"
PER=30
SEVERITY=""
STATS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --per-page) PER="$2"; shift 2;;
    --severity) SEVERITY="$2"; shift 2;;
    --stats) STATS=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

if [[ "$STATS" -eq 1 ]]; then
  [[ -f "$OUT" ]] || { echo "no cache yet: $OUT"; exit 0; }
  python3 - "$OUT" <<'PY'
import json, sys, collections
doms = collections.Counter(); n = 0
for line in open(sys.argv[1]):
    if line.strip():
        n += 1; doms[json.loads(line).get("domain","?")] += 1
print(f"{n} concepts"); print("by domain:", dict(doms.most_common()))
PY
  exit 0
fi

command -v curl >/dev/null || { echo "curl required" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"

url="https://api.github.com/advisories?per_page=$PER"
[[ -n "$SEVERITY" ]] && url="$url&severity=$SEVERITY"

hdr=(-H "Accept: application/vnd.github+json" -H "X-GitHub-Api-Version: 2022-11-28")
[[ -n "${GITHUB_TOKEN:-}" ]] && hdr+=(-H "Authorization: Bearer $GITHUB_TOKEN")

echo "fetching $url ..."
resp="$(curl -sfL "${hdr[@]}" "$url")" || {
  echo "GHSA fetch failed (rate limit or no network). Set GITHUB_TOKEN and retry." >&2
  exit 1
}
if [[ -z "$resp" ]] || [ "$(printf '%s' "$resp" | tr -d '[:space:]' | cut -c1)" != "[" ]; then
  echo "GHSA returned no JSON array (likely unauthenticated rate limit)." >&2
  echo "Set GITHUB_TOKEN=<a token> and retry for a higher limit." >&2
  exit 1
fi

respfile="$(mktemp)"
trap 'rm -f "$respfile"' EXIT
printf '%s' "$resp" > "$respfile"

python3 - "$OUT" "$respfile" <<'PY'
import json, re, sys
out_path, resp_path = sys.argv[1], sys.argv[2]
data = json.load(open(resp_path, encoding="utf-8"))
if not isinstance(data, list):
    print("unexpected API response (not a list) — likely a rate-limit message", file=sys.stderr)
    sys.exit(1)

DOMAIN_KEYS = [
    ("secrets",  r"credential|secret|token|password|api key"),
    ("sqli",     r"sql injection|sqli"),
    ("cmdi",     r"command injection|os command|arbitrary command"),
    ("ssrf",     r"server-side request forgery|ssrf"),
    ("xss",      r"cross-site scripting|xss"),
    ("xxe",      r"xml external entity|xxe"),
    ("path",     r"path traversal|directory traversal|arbitrary file"),
    ("deserial", r"deserialization|unsafe deserialization"),
    ("crypto",   r"cryptograph|weak (hash|cipher)|insecure random"),
    ("authz",    r"authorization|authentication|csrf|open redirect|access control"),
    ("injection",r"injection|template|ldap|xpath|code execution|rce"),
]
def domain_of(text):
    t = (text or "").lower()
    for name, pat in DOMAIN_KEYS:
        if re.search(pat, t): return name
    return "other"

ECOSYSTEM_LANG = {
    "npm": "js", "pip": "py", "rubygems": "rb", "maven": "java",
    "composer": "php", "go": "go", "nuget": "cs", "rust": "rs",
}

concepts = []
for a in data:
    ghsa = a.get("ghsa_id") or ""
    summary = (a.get("summary") or "").strip()
    if not ghsa or not summary:
        continue
    cwes = [c.get("cwe_id") for c in (a.get("cwes") or []) if c.get("cwe_id")]
    langs = sorted({ECOSYSTEM_LANG.get((v.get("package") or {}).get("ecosystem", ""))
                    for v in (a.get("vulnerabilities") or [])} - {None})
    concepts.append({
        "source": "ghsa",
        "concept_id": ghsa,
        "title": summary[:120],
        "domain": domain_of(summary + " " + " ".join(cwes)),
        "severity": a.get("severity") or "",
        "cwe": cwes,
        "langs": langs,
        "hint": summary[:180],
    })

concepts.sort(key=lambda c: c["concept_id"])
with open(out_path, "w", encoding="utf-8") as f:
    for c in concepts:
        f.write(json.dumps(c, ensure_ascii=False) + "\n")
print(f"wrote {out_path} ({len(concepts)} concepts)")
PY
