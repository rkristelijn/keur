#!/usr/bin/env python3
"""eval/benchmark-score.py — score each detector's benchmark run against the
AUTHORED ground truth and join with wall-clock timings.

Args: <bench-dir> <authored-gt.jsonl> <timings.tsv>

Metrics per tool:
  TP     = author-marked vulnerable loci the tool flagged (line +/-WINDOW, or
           file-level when the author label has no line)
  FN     = author loci the tool missed
  recall = TP / (TP+FN)
  FP*    = tool findings on a locus the author marked SAFE (expect:safe) — a REAL
           false positive. (Candidate: absent a safe label we cannot prove FP,
           so FP* only counts safe-label violations, honestly.)
  raw    = total findings the tool produced (volume / noise proxy)
  sec    = wall-clock seconds to scan the whole corpus
  loci/s = author loci correctly found per second (speed-adjusted usefulness)
"""
import json, os, sys, collections

WINDOW = 3

def safe_path(p, *, must_exist=True):
    """Resolve and validate a user-supplied path before any file I/O.

    Normalises to an absolute real path (collapsing any '..' traversal) and,
    when required, asserts the target exists and is a regular file. This breaks
    the raw argv -> open() taint flow: downstream opens receive a vetted path.
    """
    rp = os.path.realpath(p)
    if must_exist and not os.path.isfile(rp):
        sys.exit(f"benchmark-score: not a readable file: {p}")
    return rp

bench         = safe_path(sys.argv[1], must_exist=False)  # a directory
gt_path       = safe_path(sys.argv[2])
timings_path  = safe_path(sys.argv[3])
if not os.path.isdir(bench):
    sys.exit(f"benchmark-score: not a directory: {sys.argv[1]}")

def load(p):
    out=[]
    rp = safe_path(p, must_exist=False)
    if os.path.isfile(rp):
        with open(rp, encoding="utf-8") as fh:
            for l in fh:
                l=l.strip()
                if l:
                    try: out.append(json.loads(l))
                    except json.JSONDecodeError: pass
    return out

# authored GT: reviewed positives (with a locus) + safe labels
gt = load(gt_path)
pos = [r for r in gt if r.get("reviewed") is True and r.get("expect")=="vulnerable"]
safe = [r for r in gt if r.get("reviewed") is True and r.get("expect")=="safe"]
pos_by_repo = collections.defaultdict(list)
for r in pos: pos_by_repo[r["repo"]].append(r)
safe_keys = {(r["repo"], r["locator"].get("file",""), r.get("category","")) for r in safe}

# timings
secs={}
with open(timings_path, encoding="utf-8") as fh:
    for l in fh:
        parts=l.rstrip("\n").split("\t")
        if len(parts)>=2 and parts[0] not in ("tool",):
            try: secs[parts[0]]=float(parts[1])
            except ValueError: pass

def norm(fp):
    fp=fp or ""
    if "/repos/" in fp: fp=fp.split("/repos/",1)[1].split("/",1)[-1]
    return fp

def caught(finds, r):
    wf=r["locator"].get("file",""); wl=r["locator"].get("line")
    for (f,ln) in finds:
        if f.endswith(wf) or wf.endswith(f):
            if wl is None or abs((ln or 0)-wl)<=WINDOW:
                return True
    return False

rows=[]
tools=sorted(f[:-6] for f in os.listdir(bench) if f.endswith(".jsonl"))
for tool in tools:
    finds_by_repo=collections.defaultdict(list)
    all_finds=[]
    for x in load(os.path.join(bench, tool+".jsonl")):
        fp=norm(x.get("file",""))
        finds_by_repo[x.get("repo","")].append((fp, x.get("line",0)))
        all_finds.append((x.get("repo",""), fp, x.get("category","")))
    tp=sum(1 for r in pos if caught(finds_by_repo.get(r["repo"],[]), r))
    fn=len(pos)-tp
    fp_real=sum(1 for (repo,f,cat) in all_finds if (repo,f,cat) in safe_keys)
    raw=len(all_finds)
    sec=secs.get(tool, 0.0)
    recall = tp/(tp+fn) if (tp+fn) else 0.0
    loci_s = tp/sec if sec>0 else 0.0
    rows.append((tp, tool, fn, fp_real, raw, recall, sec, loci_s))

print("="*84)
print("KEUR BENCHMARK — detectors vs AUTHORED ground truth (repos' own vuln markers)")
print("="*84)
print(f"author vulnerable loci: {len(pos)}   safe labels: {len(safe)}   line-window: +/-{WINDOW}\n")
print(f"  {'detector':12} {'TP':>4} {'FN':>4} {'recall':>7} {'FP*':>5} {'raw':>7} {'sec':>7} {'loci/s':>7}")
for tp,tool,fn,fpr,raw,rec,sec,ls in sorted(rows, key=lambda r:(-r[0], r[6])):
    star=" <-- keur" if tool=="keur" else ""
    print(f"  {tool:12} {tp:>4} {fn:>4} {rec:>6.0%} {fpr:>5} {raw:>7} {sec:>6.1f}s {ls:>6.1f}{star}")
print()
print("  recall = share of author-marked vulns found · FP* = hits on author-safe loci")
print("  raw    = total findings (noise proxy) · sec = corpus scan time · loci/s = TP/sec")

# machine-readable
out={"loci":len(pos),"safe":len(safe),"window":WINDOW,
     "tools":[{"tool":t,"tp":tp,"fn":fn,"fp":fpr,"raw":raw,
               "recall":round(rec,4),"seconds":round(sec,2),"loci_per_sec":round(ls,2)}
              for tp,t,fn,fpr,raw,rec,sec,ls in rows]}
with open(os.path.join(bench, "benchmark.json"), "w", encoding="utf-8") as fh:
    json.dump(out, fh, indent=2)
print(f"\n  -> {os.path.join(bench,'benchmark.json')}")
