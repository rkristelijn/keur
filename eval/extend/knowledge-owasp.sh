#!/usr/bin/env bash
set -uo pipefail

# knowledge-owasp.sh — knowledge adapter #4: OWASP Top 10 (2021).
#
# The OWASP Top 10 is a small, stable, well-known list published under Creative
# Commons (CC-BY-SA). Rather than scrape the website, this adapter carries the
# ten categories (id, title, representative CWEs, domain) as curated metadata —
# the list changes only every few years. We use the category as *inspiration*;
# the generator writes fresh fictional code. Attribution: OWASP Top 10 (2021),
# © OWASP Foundation, CC-BY-SA.
#
# Usage:
#   knowledge-owasp.sh [--out FILE]
#   knowledge-owasp.sh --stats [--out FILE]

HERE="$(cd "$(dirname "$0")" && pwd -P)"
OUT="$HERE/knowledge/owasp.jsonl"
STATS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2;;
    --stats) STATS=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

if [ "$STATS" -eq 1 ]; then
  [ -f "$OUT" ] || { echo "no cache yet: $OUT"; exit 0; }
  wc -l < "$OUT" | sed 's/^/concepts: /'
  exit 0
fi

mkdir -p "$(dirname "$OUT")"

python3 - "$OUT" <<'PY'
import json, sys
out_path = sys.argv[1]

# OWASP Top 10 (2021). Representative CWEs and a coarse keur domain per item.
TOP10 = [
    ("A01:2021", "Broken Access Control", "authz",
     ["CWE-284","CWE-285","CWE-639"],
     "Access control enforces policy so users cannot act outside their permissions; failures allow privilege escalation or unauthorized data access."),
    ("A02:2021", "Cryptographic Failures", "crypto",
     ["CWE-259","CWE-327","CWE-331"],
     "Weak or missing cryptography (weak ciphers/hashes, hard-coded keys, poor randomness) exposes sensitive data."),
    ("A03:2021", "Injection", "injection",
     ["CWE-79","CWE-89","CWE-78"],
     "Untrusted input is interpreted as code or query (SQL, OS command, XSS, LDAP), letting an attacker alter execution."),
    ("A04:2021", "Insecure Design", "other",
     ["CWE-209","CWE-256","CWE-501"],
     "Missing or ineffective security controls by design, distinct from implementation bugs."),
    ("A05:2021", "Security Misconfiguration", "other",
     ["CWE-16","CWE-611"],
     "Insecure defaults, verbose errors, or misconfigured features (including XXE) that widen the attack surface."),
    ("A06:2021", "Vulnerable and Outdated Components", "other",
     ["CWE-1104"],
     "Use of components with known vulnerabilities or without maintenance/patching."),
    ("A07:2021", "Identification and Authentication Failures", "authz",
     ["CWE-287","CWE-384"],
     "Weak authentication or session management lets attackers assume other users' identities."),
    ("A08:2021", "Software and Data Integrity Failures", "deserial",
     ["CWE-502","CWE-829"],
     "Code/data integrity not verified (insecure deserialization, untrusted update sources), enabling tampering."),
    ("A09:2021", "Security Logging and Monitoring Failures", "other",
     ["CWE-778"],
     "Insufficient logging/monitoring delays or prevents detection of breaches."),
    ("A10:2021", "Server-Side Request Forgery (SSRF)", "ssrf",
     ["CWE-918"],
     "The server fetches a URL from user input without validation, reaching internal services."),
]

concepts = []
for oid, title, domain, cwes, hint in TOP10:
    concepts.append({
        "source": "owasp-top10-2021",
        "concept_id": oid,
        "title": title,
        "domain": domain,
        "severity": "",
        "cwe": cwes,
        "langs": [],
        "hint": hint[:180],
    })

with open(out_path, "w", encoding="utf-8") as f:
    for c in concepts:
        f.write(json.dumps(c, ensure_ascii=False) + "\n")
print(f"wrote {out_path} ({len(concepts)} concepts)")
PY
