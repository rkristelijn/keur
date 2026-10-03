#!/usr/bin/env bash
set -uo pipefail

# knowledge-semgrep.sh — knowledge adapter #5: Semgrep open rule registry.
#
# Reads rule METADATA (id, message, metadata.cwe, languages) from a local clone
# of Semgrep's open-source registry. We deliberately take metadata ONLY — never
# a rule's `pattern`/`patterns` or its example code — so nothing copyrightable
# from the registry ends up here, and the generator still writes fresh fictional
# code. Clone the registry yourself and point --registry at it:
#     git clone https://github.com/semgrep/semgrep-rules
#
# Usage:
#   knowledge-semgrep.sh --registry DIR [--out FILE] [--limit N]
#   knowledge-semgrep.sh --stats [--out FILE]
#
# Requires PyYAML (pip install pyyaml) to parse the rule YAML.

HERE="$(cd "$(dirname "$0")" && pwd -P)"
OUT="$HERE/knowledge/semgrep.jsonl"
REG=""
LIMIT=200
STATS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --registry) REG="$2"; shift 2;;
    --limit) LIMIT="$2"; shift 2;;
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

[[ -n "$REG" ]] || { echo "--registry DIR required (clone github.com/semgrep/semgrep-rules)" >&2; exit 1; }
[[ -d "$REG" ]] || { echo "registry dir not found: $REG" >&2; exit 1; }
python3 -c "import yaml" 2>/dev/null || { echo "PyYAML required: pip install pyyaml" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"

python3 - "$REG" "$OUT" "$LIMIT" <<'PY'
import json, os, re, sys, yaml
reg, out_path, limit = sys.argv[1], sys.argv[2], int(sys.argv[3])

DOMAIN_KEYS = [
    ("secrets",  r"secret|credential|hardcoded|api key|token|password"),
    ("sqli",     r"sql injection|sqli"),
    ("cmdi",     r"command injection|os command|subprocess.*shell"),
    ("ssrf",     r"ssrf|server-side request"),
    ("xss",      r"xss|cross-site scripting"),
    ("xxe",      r"xxe|xml external"),
    ("path",     r"path traversal|directory traversal"),
    ("deserial", r"deserial|pickle|yaml.load"),
    ("crypto",   r"crypto|weak hash|md5|insecure random"),
    ("authz",    r"auth|jwt|csrf|open redirect"),
    ("injection",r"injection|template|ldap|code execution"),
]
def domain_of(text):
    t = (text or "").lower()
    for name, pat in DOMAIN_KEYS:
        if re.search(pat, t): return name
    return "other"

concepts = []
seen = set()
for dirpath, _, files in os.walk(reg):
    if "/.git" in dirpath:
        continue
    for fn in files:
        if not (fn.endswith(".yaml") or fn.endswith(".yml")):
            continue
        try:
            doc = yaml.safe_load(open(os.path.join(dirpath, fn), encoding="utf-8", errors="ignore"))
        except Exception:
            continue
        if not isinstance(doc, dict):
            continue
        for rule in doc.get("rules", []) or []:
            rid = rule.get("id")
            msg = (rule.get("message") or "").strip()
            if not rid or rid in seen:
                continue
            seen.add(rid)
            meta = rule.get("metadata", {}) or {}
            cwe = meta.get("cwe", [])
            if isinstance(cwe, str): cwe = [cwe]
            cwe = [re.search(r"CWE-\d+", c).group(0) for c in cwe if isinstance(c, str) and re.search(r"CWE-\d+", c)]
            langs = rule.get("languages", []) or []
            hint = re.split(r"(?<=[.!?])\s", msg)[0][:180] if msg else ""
            # METADATA ONLY — we intentionally ignore rule["pattern(s)"].
            concepts.append({
                "source": "semgrep-registry",
                "concept_id": f"semgrep:{rid}",
                "title": (msg[:120] or rid),
                "domain": domain_of(f"{rid} {msg} {' '.join(cwe)}"),
                "severity": (rule.get("severity") or "").lower(),
                "cwe": cwe,
                "langs": langs,
                "hint": hint,
            })
            if len(concepts) >= limit:
                break
    if len(concepts) >= limit:
        break

concepts.sort(key=lambda c: c["concept_id"])
with open(out_path, "w", encoding="utf-8") as f:
    for c in concepts:
        f.write(json.dumps(c, ensure_ascii=False) + "\n")
print(f"wrote {out_path} ({len(concepts)} concepts)")
PY
