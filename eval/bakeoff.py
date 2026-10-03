#!/usr/bin/env python3
"""eval/bakeoff.py — compare every detector against the AUTHORED ground truth.

Sources compared:
  - the 9 proven tools (from eval/corpus/groundtruth.jsonl, per tool)
  - keur-own            (.tmp/bakeoff/keur-own.jsonl)
  - keur+cpm rules      (.tmp/bakeoff/keur-cpm.jsonl)

Ground truth: eval/corpus/authored-groundtruth.jsonl (the repos' own vuln docs).

Matching is GRANULARITY-AWARE (per docs/design/authored-ground-truth-format.md):
  line      -> a finding in the same file within +/- LINE_WINDOW lines
  file      -> a finding anywhere in that file
  directory -> a finding anywhere under that directory
  function  -> a finding in a file whose path or rule mentions the sink name
  class/repo-> COARSE: did the tool find ANYTHING of that category in the repo
               (recall signal only — never used to charge an FP)

Because authored GT only marks VULNERABLE loci (no exhaustive 'safe' labels),
we report per-tool, per-repo:
  TP  = authored-vulnerable locus the tool also flagged
  FN  = authored-vulnerable locus the tool missed
  FLAGS = total findings the tool raised in that repo (its raw volume)
  extra = FLAGS not matched to any authored locus (candidate FP OR real find
          the docs did not enumerate — reported, not blamed, since the authored
          GT is not exhaustive over safe code)
Recall = TP / (TP+FN). We do NOT claim precision against a non-exhaustive GT;
'extra' is surfaced so a human can adjudicate it into the GT (the enrichment loop).
"""
import json, os, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
GT = os.path.join(HERE, "corpus", "authored-groundtruth.jsonl")
SCANNER_GT = os.path.join(HERE, "corpus", "groundtruth.jsonl")
BAKE = os.path.join(HERE, "..", ".tmp", "bakeoff")
LINE_WINDOW = 3

def load_jsonl(p):
    out = []
    if not os.path.exists(p): return out
    for l in open(p):
        l = l.strip()
        if not l: continue
        try: out.append(json.loads(l))
        except Exception: pass
    return out

# --- authored ground truth, grouped by repo ---------------------------------
authored = load_jsonl(GT)
# The user's directive: use the REAL author-left ground truth. Score against the
# adjudicated (reviewed=true) labels — the author's own markers + verified loci —
# unless KEUR_GT_ALL=1 asks for every candidate.
if os.environ.get("KEUR_GT_ALL") != "1":
    authored = [r for r in authored if r.get("reviewed") is True]
gt_by_repo = collections.defaultdict(list)
for r in authored:
    gt_by_repo[r["repo"]].append(r)

# --- collect every detector's findings, grouped by (source, repo) -----------
findings = collections.defaultdict(list)   # (source, repo) -> [finding]

# proven tools: split the scanner GT per tool
for f in load_jsonl(SCANNER_GT):
    tool = f.get("tool", "?")
    repo = f.get("repo", "")
    # scanner GT 'file' is sometimes absolute under repos/<repo>/...
    fp = f.get("file", "") or ""
    if "/repos/" in fp:
        fp = fp.split("/repos/", 1)[1].split("/", 1)[-1]
    findings[(tool, repo)].append({"file": fp, "line": f.get("line", 0),
                                   "category": f.get("category", ""), "rule": f.get("rule", "")})

# keur-own + keur+cpm
for src, path in (("keur-own", os.path.join(BAKE, "keur-own.jsonl")),
                  ("keur+cpm", os.path.join(BAKE, "keur-cpm.jsonl"))):
    for f in load_jsonl(path):
        findings[(src, f.get("repo", ""))].append(
            {"file": f.get("file", ""), "line": f.get("line", 0),
             "category": f.get("category", ""), "rule": f.get("rule", "")})

SOURCES = sorted({s for (s, _) in findings})

# --- granularity-aware match: does `flags` cover authored locus `g`? --------
def covered(g, flags):
    gran = g["granularity"]; loc = g["locator"]
    if gran == "line":
        want_f, want_l = loc.get("file", ""), loc.get("line", 0)
        for x in flags:
            if x["file"].endswith(want_f) or want_f.endswith(x["file"]):
                if abs((x["line"] or 0) - want_l) <= LINE_WINDOW:
                    return True
        return False
    if gran == "file":
        want = loc.get("file", "")
        return any(x["file"].endswith(want) or want.endswith(x["file"]) for x in flags)
    if gran == "directory":
        d = loc.get("directory", "")
        return any(("/" + d + "/") in ("/" + x["file"]) or x["file"].startswith(d) for x in flags)
    if gran == "function":
        fn = (loc.get("function", "") or "").split(".")[-1].lower()
        return any(fn and (fn in x["file"].lower() or fn in x["rule"].lower()) for x in flags)
    # class / repo: coarse recall — any finding at all in the repo counts
    return len(flags) > 0

# --- score each source over each repo that HAS authored labels --------------
# GT here is VULNERABLE-only, so TP/FN are exact but FP/TN need a negative set.
# We derive a principled negative set (see below) and report FP/TN as CANDIDATE
# (confidence-labelled), never as if the GT were exhaustive over safe code.
labeled_repos = sorted(gt_by_repo)
per_source = collections.defaultdict(lambda: {"tp": 0, "fn": 0, "flags": 0,
                                              "fp": 0, "tn": 0})
per_repo_source = collections.defaultdict(dict)  # repo -> source -> (tp,fn,flags,fp,tn)

# Negative (safe) locus set, derived per repo:
#   A finding is a CANDIDATE FALSE POSITIVE for a source when it is
#     (a) not covered by any authored vulnerable locus, AND
#     (b) not corroborated by any OTHER detector at the same (file,category).
#   Rationale: with a vulnerable-only GT we cannot prove a locus is safe, but a
#   lone, uncorroborated finding that the authored docs never mention is the
#   strongest available FALSE-POSITIVE signal — exactly the class the user has
#   been hand-squashing. Corroborated-but-unlabelled findings are held out (they
#   are more likely real finds the docs did not enumerate) — they go to the
#   enrichment queue, not the FP count.
#   CANDIDATE TRUE NEGATIVE for a source = a (file,category) that ANOTHER tool
#   flagged as a candidate-FP but THIS source correctly did NOT flag.

def loc_key(x):
    return (x["file"], x["category"])

for repo in labeled_repos:
    gts = gt_by_repo[repo]
    # what every source flagged in this repo, keyed for corroboration
    flagged_by = {}  # source -> set((file,category))
    for src in SOURCES:
        flagged_by[src] = {loc_key(x) for x in findings.get((src, repo), [])}
    # a (file,category) is "corroborated" if >=2 distinct sources flag it
    keycount = collections.Counter()
    for src in SOURCES:
        for k in flagged_by[src]:
            keycount[k] += 1

    # candidate-FP keys per source: flagged by this source, not covered by an
    # authored vuln locus, and flagged by NO other source (keycount==1)
    def covers_authored(fc):
        f, c = fc
        for g in gts:
            gran = g["granularity"]; loc = g["locator"]
            gf = loc.get("file", "") or loc.get("directory", "") or ""
            if gran in ("line", "file") and gf and (f.endswith(gf) or gf.endswith(f)):
                return True
            if gran == "directory" and gf and (("/" + gf + "/") in ("/" + f)):
                return True
            if gran in ("class", "repo"):
                return True  # coarse label 'claims' the whole repo — don't count FP here
        return False

    cand_fp = {}
    for src in SOURCES:
        fps = {k for k in flagged_by[src]
               if keycount[k] == 1 and not covers_authored(k)}
        cand_fp[src] = fps
    # union of all candidate-FP keys = the derived NEGATIVE-ish locus set
    neg_universe = set().union(*cand_fp.values()) if cand_fp else set()

    for src in SOURCES:
        flags = findings.get((src, repo), [])
        tp = sum(1 for g in gts if covered(g, flags))
        fn = len(gts) - tp
        fp = len(cand_fp[src])
        # candidate TN: negative loci this source did NOT flag
        tn = len(neg_universe - flagged_by[src])
        s = per_source[src]
        s["tp"] += tp; s["fn"] += fn; s["flags"] += len(flags); s["fp"] += fp; s["tn"] += tn
        per_repo_source[repo][src] = (tp, fn, len(flags), fp, tn)

# --- report -----------------------------------------------------------------
print("=" * 74)
print("BAKE-OFF — detectors vs AUTHORED ground truth (the repos' own vuln docs)")
print("=" * 74)
print(f"authored labels: {len(authored)} across {len(labeled_repos)} repos")
print(f"line-match window: +/-{LINE_WINDOW} lines\n")

print("PER-TOOL confusion vs authored loci. TP/FN are EXACT (GT marks vulnerable);")
print("FP/TN are CANDIDATE — derived: a lone, uncorroborated finding the docs never")
print("mention is a candidate FP; a candidate-safe locus another tool flagged but")
print("this one did not is a candidate TN. Precision* is against candidate-FP only.")
print(f"  {'source':16} {'TP':>4} {'FN':>4} {'FP*':>5} {'TN*':>5} {'recall':>7} {'prec*':>6} {'raw':>7}")
rows = []
for src in SOURCES:
    s = per_source[src]
    tot = s["tp"] + s["fn"]
    rec = s["tp"] / tot if tot else 0.0
    prec = s["tp"] / (s["tp"] + s["fp"]) if (s["tp"] + s["fp"]) else 0.0
    rows.append((s["tp"], src, s["fn"], s["fp"], s["tn"], rec, prec, s["flags"]))
for tp, src, fn, fp, tn, rec, prec, fl in sorted(rows, reverse=True):
    print(f"  {src:16} {tp:>4} {fn:>4} {fp:>5} {tn:>5} {rec:>6.0%} {prec:>5.0%} {fl:>7}")
print("\n  * FP/TN/prec are candidate metrics against a vulnerable-only GT — a lower")
print("    FP* + high recall is the sweet spot; huge raw-flags with low prec* = noise.")

print("\nPER-REPO — who finds the authored loci (TP/total), and raw volume:")
for repo in labeled_repos:
    total = len(gt_by_repo[repo])
    best = max(SOURCES, key=lambda s: per_repo_source[repo][s][0])
    bt = per_repo_source[repo][best][0]
    print(f"\n  {repo}  ({total} authored loci)")
    for src in SOURCES:
        tp, fn, fl, fp, tn = per_repo_source[repo][src]
        mark = "  <- best" if src == best and bt > 0 else ""
        if tp or fl:
            print(f"    {src:16} TP={tp:>3}/{total:<3} FP*={fp:>4} flags={fl:>4}{mark}")

# --- repo classification ----------------------------------------------------
print("\n" + "=" * 74)
print("REPO CLASSIFICATION")
print("=" * 74)
all_repos = sorted({os.path.basename(p) for p in
                    os.listdir(os.path.join(HERE, "corpus", "repos"))
                    if os.path.isdir(os.path.join(HERE, "corpus", "repos", p))})

auto = []       # authored labels AND a tool corroborates at least one
doconly = []    # authored labels but NO tool finds any locus
noauthored = [] # no authored labels harvested at all (doc-only / needs adapter)

for repo in all_repos:
    if repo not in gt_by_repo:
        noauthored.append(repo); continue
    any_tp = any(per_repo_source[repo][s][0] > 0 for s in SOURCES)
    (auto if any_tp else doconly).append(repo)

print(f"\nAUTOMATABLE ({len(auto)}) — authored labels corroborated by >=1 detector,")
print("  so the ground truth for these can be set automatically (tool locus == label):")
for r in auto: print(f"    {r}")

print(f"\nDOC-ONLY-LABELLED ({len(doconly)}) — authored labels exist but NO detector")
print("  hit the locus. Either the label is too coarse (class/repo) or a real FN:")
for r in doconly: print(f"    {r}")

print(f"\nNO-AUTHORED-LABELS ({len(noauthored)}) — no adapter parsed vuln docs yet.")
print("  These need an adapter or LLM-prose enrichment before they can be labelled:")
for r in noauthored: print(f"    {r}")

# --- enrichment proposal: tool consensus as candidate labels ----------------
print("\n" + "=" * 74)
print("GROUND-TRUTH ENRICHMENT PROPOSAL")
print("=" * 74)
print("For NO-AUTHORED-LABELS repos, findings CORROBORATED by multiple independent")
print("tools are the strongest candidate labels to adjudicate into the GT.\n")
PROVEN = [s for s in SOURCES if s not in ("keur-own", "keur+cpm")]
for repo in noauthored:
    # count, per (file,category), how many DISTINCT proven tools agree
    agree = collections.defaultdict(set)
    for tool in PROVEN:
        for x in findings.get((tool, repo), []):
            agree[(x["file"], x["category"])].add(tool)
    strong = [(k, v) for k, v in agree.items() if len(v) >= 2]
    if strong:
        print(f"  {repo}: {len(strong)} loci with >=2 tools agreeing "
              f"(top: {sorted(strong, key=lambda kv: -len(kv[1]))[0][0][0] or '(repo)'})")
