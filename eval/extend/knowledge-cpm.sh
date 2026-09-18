#!/usr/bin/env bash
set -euo pipefail

# knowledge-cpm.sh — knowledge adapter #1: cpm's security rule corpus.
#
# Extracts a normalised "concept item" from every `category: security` rule in
# cpm (id, title, category, severity, languages, CWE, a pattern hint) and writes
# ONE JSONL line per concept to the knowledge cache. This is the FIRST adapter
# behind a stable contract (see eval/extend/README.md): later adapters
# (CWE/MITRE, OWASP, Semgrep registry, GitHub advisories) emit the SAME shape,
# so the generator downstream does not care where a concept came from.
#
# The knowledge is source-agnostic. We use cpm's rules as INSPIRATION only — the
# generator produces fresh FICTIONAL code, never copying any rule's example.
#
# Normalised concept schema (one JSON object per line):
#   {
#     "source":   "cpm-rules",           # which adapter produced this
#     "concept_id":"cpm:BE-SEC-015",     # stable id within the source
#     "title":    "Server-side request forgery (SSRF)",
#     "domain":   "ssrf",                # coarse security class (best-effort)
#     "severity": "error",
#     "cwe":      ["CWE-918"],           # any CWE ids found in the rule text
#     "langs":    ["ts","js","py",...],  # from target.extensions
#     "hint":     "user input in HTTP request URL — SSRF"  # first message, trimmed
#   }
#
# Usage:
#   knowledge-cpm.sh [--out FILE] [--cpm DIR]
#     --out   knowledge cache JSONL (default: eval/extend/knowledge/cpm.jsonl)
#     --cpm   cpm repo path (default: $CPM_HOME or ~/git/hub/cpm)
#   knowledge-cpm.sh --stats [--out FILE]   summarise an existing cache

HERE="$(cd "$(dirname "$0")" && pwd -P)"
CPM="${CPM_HOME:-$HOME/git/hub/cpm}"
OUT="$HERE/knowledge/cpm.jsonl"
STATS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --cpm) CPM="$2"; shift 2;;
    --stats) STATS=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

if [ "$STATS" -eq 1 ]; then
  [ -f "$OUT" ] || { echo "no cache yet: $OUT"; exit 0; }
  python3 - "$OUT" <<'PY'
import json, sys, collections
doms = collections.Counter(); cwe = collections.Counter(); n = 0
for line in open(sys.argv[1]):
    line = line.strip()
    if not line: continue
    c = json.loads(line); n += 1
    doms[c.get("domain","?")] += 1
    for w in c.get("cwe", []): cwe[w] += 1
print(f"{n} concepts")
print("by domain:", dict(doms.most_common()))
print("top CWE:  ", dict(cwe.most_common(10)))
PY
  exit 0
fi

[ -d "$CPM/rules" ] || { echo "cpm rules not found: $CPM/rules" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"

python3 - "$CPM/rules" "$OUT" <<'PY'
import os, re, json, sys
rules_dir, out_path = sys.argv[1], sys.argv[2]

# Coarse domain from id/title/category keywords. Best-effort; the generator
# only uses it to group prompts, never as ground truth.
DOMAIN_KEYS = [
    ("secrets",  r"secret|api[_-]?key|token|password|credential|private key"),
    ("sqli",     r"sql injection|sqli|raw sql|query concat"),
    ("cmdi",     r"command injection|shell|exec|system\(|popen"),
    ("ssrf",     r"ssrf|server-side request"),
    ("xss",      r"xss|cross-site script|innerhtml|v-html"),
    ("xxe",      r"xxe|xml external|xml bomb"),
    ("path",     r"path traversal|directory traversal|lfi|file inclusion"),
    ("deserial", r"deserial|pickle|unmarshal|yaml.load"),
    ("crypto",   r"weak (hash|cipher|random)|md5|sha1|insecure random|unsafe_random"),
    ("authz",    r"auth|jwt|session|cookie|cors|open redirect|mass assignment"),
    ("injection",r"ldap injection|nosql|ssti|template injection|log injection"),
]

def domain_of(text):
    t = text.lower()
    for name, pat in DOMAIN_KEYS:
        if re.search(pat, t):
            return name
    return "other"

def field(block, key):
    m = re.search(rf"^{key}:\s*(.+)$", block, re.M)
    return m.group(1).strip() if m else ""

concepts = []
for dirpath, _, files in os.walk(rules_dir):
    for fn in sorted(files):
        if not fn.endswith(".rule"):
            continue
        text = open(os.path.join(dirpath, fn), encoding="utf-8", errors="ignore").read()
        if "category: security" not in text:
            continue
        rid = field(text, "id")
        title = field(text, "title")
        severity = field(text, "severity")
        # languages from target.extensions (.ts .js -> ts js)
        ext_m = re.search(r"extensions:\s*(.+)", text)
        langs = []
        if ext_m:
            langs = [e.lstrip(".") for e in ext_m.group(1).split() if e.startswith(".")]
        # CWE ids anywhere in the rule text
        cwe = sorted(set(re.findall(r"CWE-\d+", text)))
        # first pattern message as a human hint (trim to keep it short)
        msg_m = re.search(r'message:\s*"([^"]+)"', text)
        hint = ""
        if msg_m:
            hint = msg_m.group(1).strip()
            hint = re.split(r"(?<=[.!?])\s", hint)[0][:160]  # first sentence, capped
        if not rid or not title:
            continue
        concepts.append({
            "source": "cpm-rules",
            "concept_id": f"cpm:{rid}",
            "title": title,
            "domain": domain_of(f"{rid} {title} {hint}"),
            "severity": severity,
            "cwe": cwe,
            "langs": langs,
            "hint": hint,
        })

concepts.sort(key=lambda c: c["concept_id"])
with open(out_path, "w", encoding="utf-8") as f:
    for c in concepts:
        f.write(json.dumps(c, ensure_ascii=False) + "\n")
print(f"wrote {out_path} ({len(concepts)} concepts)")
PY
