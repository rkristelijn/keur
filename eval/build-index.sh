#!/usr/bin/env bash
set -euo pipefail

# build-index.sh — regenerate keur-eval/index.json from the KEUR-EXPECT
# annotations in every case file. The evaluator reads index.json, not the files.
#
# Usage: build-index.sh   (KEUR_EVAL env overrides repo path)

KEUR_EVAL="${KEUR_EVAL:-$HOME/git/hub/keur-eval}"
[[ -d "$KEUR_EVAL" ]] || { echo "keur-eval not found: $KEUR_EVAL" >&2; exit 1; }

python3 - "$KEUR_EVAL" <<'PY'
import os, re, json, sys
root = sys.argv[1]
cases = []
rx_expect     = re.compile(r'KEUR-EXPECT:\s*(.+?)\s*(?:-->)?\s*$')
rx_expect_not = re.compile(r'KEUR-EXPECT-NOT:\s*(.+?)\s*(?:-->)?\s*$')
rx_cat        = re.compile(r'KEUR-CATEGORY:\s*(\S+)')
for cls in ("positive", "negative", "regression"):
    base = os.path.join(root, cls)
    if not os.path.isdir(base): continue
    for dirpath, _, files in os.walk(base):
        for fn in files:
            if fn == ".gitkeep" or fn.startswith("."):
                continue  # placeholders / dotfiles are not cases
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, root)
            expect, expect_not, cat = [], [], ""
            try:
                head = open(p, encoding="utf-8", errors="ignore").read(1500)
            except Exception:
                continue
            for line in head.splitlines():
                m = rx_expect.search(line)
                if m and "EXPECT-NOT" not in line:
                    v = m.group(1).strip()
                    expect = [] if v.lower() == "none" else v.split()
                m2 = rx_expect_not.search(line)
                if m2: expect_not = m2.group(1).strip().split()
                m3 = rx_cat.search(line)
                if m3: cat = m3.group(1)
            cases.append({"path": rel, "class": cls, "category": cat,
                          "expect": expect, "expect_not": expect_not})
out = os.path.join(root, "index.json")
json.dump({"cases": sorted(cases, key=lambda c: c["path"])}, open(out, "w"), indent=2)
print(f"wrote {out} ({len(cases)} cases)")
PY
