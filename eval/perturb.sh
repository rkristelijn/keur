#!/usr/bin/env bash
# eval/perturb.sh — produce a REFORMATTED variant of a training case.
#
# The slop test (optie B): a candidate rule that fires on the original case but
# NOT on this perturbed variant learned the FORM (the corpus's tidy "Label:
# value" layout) rather than the CONCEPT (the sensitive value itself). The
# perturbation keeps the SEMANTIC content — IPs, monetary figures, hostnames,
# identifiers — but strips the annotation-like structure and rewrites the
# layout, so a form-bound regex misses while a concept-bound regex still fires.
#
# Deterministic: same input -> same output (no randomness), so the gate is
# reproducible.
#
# Usage: perturb.sh <case-file>   -> writes perturbed content to stdout
set -uo pipefail

SRC="${1:?case file required}"
[[ -f "$SRC" ]] || { echo "perturb: not found: $SRC" >&2; exit 1; }

python3 - "$SRC" <<'PY'
import sys, re, os

path = sys.argv[1]
text = open(path, encoding="utf-8", errors="ignore").read()
ext = os.path.splitext(path)[1].lower()

# Two perturbation strategies, because "form" means different things for code vs
# prose:
#   - CODE (.py .js .java ...): the FORM is the surrounding layout/identifiers,
#     the CONCEPT is the dangerous call (os.system(...), query(...+...)). Keep
#     the code STRUCTURE intact (newlines, parens) but rename identifiers and
#     reflow whitespace, so a concept-bound rule (matching the sink) still fires
#     while a rule that memorised variable names / exact spacing misses.
#   - PROSE (.md .txt): the FORM is the tidy "Label: value" layout the corpus
#     model emitted; the CONCEPT is the sensitive value. Strip the label
#     structure, keep the values, reflow to one paragraph.
CODE_EXT = {".py", ".js", ".ts", ".java", ".go", ".rb", ".php", ".sh",
            ".c", ".cpp", ".cs", ".rs"}

def strip_annotations(t):
    return "\n".join(l for l in t.splitlines()
                     if not re.search(r'KEUR-(EXPECT|CATEGORY|NOTE)', l))

if ext in CODE_EXT:
    t = strip_annotations(text)
    # rename common user-input identifiers so a name-bound regex misses, but the
    # sink call stays syntactically intact.
    renames = {
        r'\buser_input\b': 'req_param', r'\bcommand\b': 'cmd_str',
        r'\bquery\b': 'sql_stmt', r'\bpassword\b': 'secret_val',
        r'\bdata\b': 'payload',
    }
    for pat, rep in renames.items():
        t = re.sub(pat, rep, t)
    # normalise indentation to a single space level but KEEP newlines & parens
    t = "\n".join(re.sub(r'^\s+', ' ', l) for l in t.splitlines())
    print(t)
else:
    lines = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or re.search(r'KEUR-(EXPECT|CATEGORY|NOTE)', s):
            continue
        s = re.sub(r'[*_`#>-]+', ' ', s)
        m = re.match(r'\s*([A-Za-z][\w /()]{1,40}?)\s*[:=]\s*(.+)$', s)
        if m:
            value = m.group(2).strip().strip('"\'')
            s = f"the recorded item {value} appears here"
        s = re.sub(r'\s+', ' ', s).strip()
        if s:
            lines.append(s)
    print(" ".join(lines))
PY
