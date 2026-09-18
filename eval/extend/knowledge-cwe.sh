#!/usr/bin/env bash
set -uo pipefail

# knowledge-cwe.sh — knowledge adapter #2: the CWE catalog (MITRE).
#
# Downloads the official CWE "Software Development" view (CWE-699) as CSV — the
# developer-relevant weakness subset — and normalises each weakness into a
# concept item. CWE is free to use with attribution (MITRE Terms of Use); the
# download is a plain, unauthenticated file. We use the weakness *concept* only;
# the generator writes fresh fictional code, never copying CWE's examples.
#
# Source:  https://cwe.mitre.org/data/csv/699.csv.zip   (@see SOURCES.md)
# Attribution: CWE™ is a trademark of The MITRE Corporation.
#
# Usage:
#   knowledge-cwe.sh [--out FILE] [--csv FILE]
#     --csv   use a pre-downloaded 699.csv(.zip) instead of fetching (offline)
#     --out   knowledge cache JSONL (default eval/extend/knowledge/cwe.jsonl)
#   knowledge-cwe.sh --stats [--out FILE]

HERE="$(cd "$(dirname "$0")" && pwd -P)"
OUT="$HERE/knowledge/cwe.jsonl"
CSV_URL="https://cwe.mitre.org/data/csv/699.csv.zip"
CSV_LOCAL=""
STATS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --csv) CSV_LOCAL="$2"; shift 2;;
    --stats) STATS=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

if [ "$STATS" -eq 1 ]; then
  [ -f "$OUT" ] || { echo "no cache yet: $OUT"; exit 0; }
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

mkdir -p "$(dirname "$OUT")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Obtain the CSV: use a local copy if given, else download (needs curl+unzip).
csv=""
if [ -n "$CSV_LOCAL" ]; then
  case "$CSV_LOCAL" in
    *.zip) unzip -o -q "$CSV_LOCAL" -d "$tmp" && csv="$(ls "$tmp"/*.csv | head -1)";;
    *)     csv="$CSV_LOCAL";;
  esac
else
  command -v curl >/dev/null || { echo "curl required (or pass --csv FILE)" >&2; exit 1; }
  echo "downloading $CSV_URL ..."
  if ! curl -sfL "$CSV_URL" -o "$tmp/699.csv.zip"; then
    echo "download failed (no network?). Fetch 699.csv.zip manually and pass --csv." >&2
    exit 1
  fi
  command -v unzip >/dev/null || { echo "unzip required" >&2; exit 1; }
  unzip -o -q "$tmp/699.csv.zip" -d "$tmp"
  csv="$(ls "$tmp"/*.csv | head -1)"
fi
[ -f "$csv" ] || { echo "no CSV found" >&2; exit 1; }

python3 - "$csv" "$OUT" <<'PY'
import csv, json, re, sys
csv_path, out_path = sys.argv[1], sys.argv[2]

DOMAIN_KEYS = [
    ("secrets",  r"credential|password|secret|hard-coded|hardcoded key"),
    ("sqli",     r"sql injection"),
    ("cmdi",     r"os command injection|command injection|argument injection"),
    ("ssrf",     r"server-side request forgery|ssrf"),
    ("xss",      r"cross-site scripting|xss"),
    ("xxe",      r"xml external entity|xxe|xml entity"),
    ("path",     r"path traversal|link following|file inclusion"),
    ("deserial", r"deserialization"),
    ("crypto",   r"cryptograph|weak hash|weak password requirements|insufficient entropy|random"),
    ("authz",    r"authorization|authentication|csrf|cross-site request|session|redirect"),
    ("injection",r"injection|ldap|xpath|code injection|template"),
]
def domain_of(text):
    t = text.lower()
    for name, pat in DOMAIN_KEYS:
        if re.search(pat, t): return name
    return "other"

def col(row, *names):
    for n in names:
        if n in row and row[n]: return row[n]
    return ""

concepts = []
with open(csv_path, newline="", encoding="utf-8", errors="ignore") as f:
    reader = csv.DictReader(f)
    for row in reader:
        cwe_id = col(row, "CWE-ID", "\ufeffCWE-ID", "ID")
        name   = col(row, "Name")
        desc   = col(row, "Description")
        if not cwe_id or not name:
            continue
        # keep only concrete weaknesses (skip categories/views/pillars if typed)
        wtype = col(row, "Weakness Abstraction", "Abstraction")
        hint = re.split(r"(?<=[.!?])\s", desc)[0][:180] if desc else ""
        concepts.append({
            "source": "cwe-mitre",
            "concept_id": f"CWE-{cwe_id}",
            "title": name,
            "domain": domain_of(f"{name} {hint}"),
            "severity": "",
            "cwe": [f"CWE-{cwe_id}"],
            "langs": [],
            "hint": hint,
        })

concepts.sort(key=lambda c: int(c["concept_id"].split("-")[1]) if c["concept_id"].split("-")[1].isdigit() else 0)
with open(out_path, "w", encoding="utf-8") as f:
    for c in concepts:
        f.write(json.dumps(c, ensure_ascii=False) + "\n")
print(f"wrote {out_path} ({len(concepts)} concepts)")
PY
