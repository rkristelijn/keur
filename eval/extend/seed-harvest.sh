#!/usr/bin/env bash
# eval/extend/seed-harvest.sh — build a SEED QUEUE for the seed-iterate loop.
#
# The cold-start optimiser hallucinates a rule from nothing and almost always
# overfits (see eval/history/attempts.tsv: hundreds of overfit-regex rejects).
# A far better attempt-0 is an EXISTING, proven rule. This script collects such
# seeds from two sources and writes them, deduplicated, to a queue that
# `optimise.sh seed-iterate` consumes:
#
#   1. CPM rules  (~/git/hub/cpm/rules/**/*.rule) that keur does NOT already
#      have. keur's .rule format is cpm-compatible, so these are candidate rules
#      as-is; the loop measures each against the corpus and only keeps what
#      passes the gate. Rules whose engine keur does not yet support
#      (file-absence/file-presence/extract-duplicates/block-absence) are NOT
#      dropped — they are parked in seeds-unsupported.jsonl as a backlog for
#      extending the engine (see docs/rules-authoring.md engines list).
#
#   2. Rejected near-misses (eval/history/attempts.tsv, reason=overfit-regex)
#      whose regex was actually GENERALISING (a real API/sink name, not a
#      memorised literal) but got shot down by the static guard.
#
# READ-ONLY w.r.t. the existing loop: writes only the queue files, touches
# nothing in optimise.sh / supervise.sh / rules/.
#
# Usage:
#   seed-harvest.sh                 build the queue (both sources)
#   seed-harvest.sh --cpm-only      only CPM rules
#   seed-harvest.sh --rejects-only  only rejected near-misses
#   seed-harvest.sh --stats         print what WOULD be queued, write nothing
#
# Output:
#   eval/.run/seeds.jsonl              queue for seed-iterate (supported engines)
#   eval/.run/seeds-unsupported.jsonl  backlog: rules needing new engines
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd -P)"          # -> eval/
KEUR_HOME="$(cd "$HERE/.." && pwd -P)"
CPM_RULES="${CPM_RULES:-$HOME/git/hub/cpm/rules}"
KEUR_RULES="$KEUR_HOME/rules"
ATTEMPTS="$HERE/history/attempts.tsv"
QUEUE="$HERE/.run/seeds.jsonl"
UNSUPPORTED="$HERE/.run/seeds-unsupported.jsonl"
mkdir -p "$HERE/.run"

MODE="${1:-both}"
case "$MODE" in
  --cpm-only)     SRC_CPM=1; SRC_REJ=0; WRITE=1 ;;
  --rejects-only) SRC_CPM=0; SRC_REJ=1; WRITE=1 ;;
  --stats)        SRC_CPM=1; SRC_REJ=1; WRITE=0 ;;
  both|*)         SRC_CPM=1; SRC_REJ=1; WRITE=1 ;;
esac

python3 - "$CPM_RULES" "$KEUR_RULES" "$ATTEMPTS" "$QUEUE" "$UNSUPPORTED" "$SRC_CPM" "$SRC_REJ" "$WRITE" <<'PY'
import sys, os, json, glob, re, hashlib

cpm_dir, keur_rules, attempts, queue, unsupported, src_cpm, src_rej, write = sys.argv[1:9]
src_cpm, src_rej, write = int(src_cpm), int(src_rej), int(write)

# --- existing keur rule IDs (dedup key): keur keeps the cpm ID on import ----
existing_ids = set()
for path in glob.glob(os.path.join(keur_rules, '**', '*.rule'), recursive=True):
    try:
        for line in open(path, encoding='utf-8', errors='replace'):
            if line.startswith('id:'):
                existing_ids.add(line.split(':', 1)[1].strip())
                break
    except OSError:
        pass

SUPPORTED = {'pattern', 'presence', 'absence'}

def rule_field(text, key):
    m = re.search(r'^%s:\s*(.+)$' % re.escape(key), text, re.M)
    return m.group(1).strip() if m else ''

def first_regex(text):
    m = re.search(r'^\s*-\s*regex:\s*(.+)$', text, re.M)
    return m.group(1).strip() if m else ''

# keur category set (the loop scores by these). Map cpm's finer categories down.
CAT_MAP = {
    'transport':'security', 'session':'security', 'oauth':'security',
    'template':'security', 'crypto':'security', 'web':'security',
    'sca':'supply-chain', 'supply-chain':'supply-chain', 'cicd':'supply-chain',
    'secret':'secret', 'secrets':'secret',
    'quality':'quality', 'style':'quality', 'err':'quality', 'perf':'quality',
    'doc':'quality', 'test':'quality', 'a11y':'quality',
}
def map_cat(c):
    c = (c or '').lower()
    return CAT_MAP.get(c, c if c in ('security','secret','supply-chain','quality','pii','iac') else 'security')

seeds = []
parked = []           # unsupported-engine rules: backlog, not dropped
seen_regex = set()

# ---- source 1: CPM rules ---------------------------------------------------
cpm_seen = cpm_kept = cpm_dup_id = cpm_dup_rx = cpm_parked = 0
if src_cpm:
    for path in sorted(glob.glob(os.path.join(cpm_dir, '**', '*.rule'), recursive=True)):
        cpm_seen += 1
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        rid = rule_field(text, 'id')
        eng = rule_field(text, 'engine') or 'pattern'
        cat = rule_field(text, 'category')
        rx  = first_regex(text)
        if rid in existing_ids:
            cpm_dup_id += 1; continue
        if eng not in SUPPORTED:
            parked.append({'source':'cpm', 'id':rid, 'engine':eng,
                           'category':map_cat(cat), 'path':path,
                           'reason':'engine-not-supported', 'rule':text})
            cpm_parked += 1; continue
        if rx and rx in seen_regex:
            cpm_dup_rx += 1; continue
        if rx:
            seen_regex.add(rx)
        if not re.search(r'^source:', text, re.M):
            text = text.rstrip() + '\nsource: cpm\n'
        seeds.append({'source':'cpm', 'id':rid, 'category':map_cat(cat),
                      'path':path, 'rule':text})
        cpm_kept += 1

# ---- source 2: rejected near-misses (generalising regex, wrongly guarded) --
rej_seen = rej_kept = 0
def looks_generalising(rx):
    """USES character classes/quantifiers/known sink names, not a memorised
    literal. Heuristic: has regex metachars AND no long literal run that looks
    like a copied value (>24 consecutive literal chars)."""
    if not rx:
        return False
    if not re.search(r'[\\\[\](){}+*?|]', rx):
        return False
    longest = max((len(m) for m in re.findall(r'[A-Za-z0-9_ ]+', rx)), default=0)
    return longest <= 24

if src_rej and os.path.exists(attempts):
    for line in open(attempts, encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) < 7:
            continue
        _, _, _, cat, dec, reason, rx = p[:7]
        if dec != 'retry' or reason != 'overfit-regex':
            continue
        rej_seen += 1
        if not looks_generalising(rx) or rx in seen_regex:
            continue
        seen_regex.add(rx)
        rid = 'SEED-REJ-' + hashlib.sha1(rx.encode()).hexdigest()[:8].upper()
        rule = ("id: %s\n"
                "title: Recovered from rejected near-miss (%s)\n"
                "category: %s\n"
                "severity: warning\n"
                "engine: pattern\n"
                "source: rejected-near-miss\n"
                "target:\n"
                "  extensions: .js .ts .py .php .rb .go .java .yml .yaml\n"
                "patterns:\n"
                "  - regex: %s\n"
                "    message: %s issue (recovered seed; refine me)\n"
                % (rid, cat, map_cat(cat), rx, cat))
        seeds.append({'source':'rejected-near-miss', 'id':rid,
                      'category':map_cat(cat), 'path':'', 'rule':rule})
        rej_kept += 1

# ---- report + write --------------------------------------------------------
sys.stderr.write(
    "seed-harvest:\n"
    "  CPM   : seen=%d kept=%d (dup-id=%d dup-regex=%d parked-unsupported=%d)\n"
    "  REJECT: seen=%d kept=%d\n"
    "  TOTAL queued: %d   parked (engine backlog): %d\n"
    % (cpm_seen, cpm_kept, cpm_dup_id, cpm_dup_rx, cpm_parked,
       rej_seen, rej_kept, len(seeds), len(parked)))

if write:
    with open(queue, 'w', encoding='utf-8') as f:
        for s in seeds:
            f.write(json.dumps(s) + '\n')
    with open(unsupported, 'w', encoding='utf-8') as f:
        for s in parked:
            f.write(json.dumps(s) + '\n')
    sys.stderr.write("  wrote %s (%d) and %s (%d)\n"
                     % (queue, len(seeds), unsupported, len(parked)))
else:
    sys.stderr.write("  (--stats: nothing written)\n")
PY
