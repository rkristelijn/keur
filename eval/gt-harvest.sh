#!/usr/bin/env bash
# eval/gt-harvest.sh — harvest AUTHORED ground truth from each corpus repo's own
# vulnerability documentation (sinks.json, challenge docs, per-vuln READMEs) into
# one common JSONL: eval/corpus/authored-groundtruth.jsonl.
#
# WHY: eval/groundtruth.sh builds ground truth from SCANNER OUTPUT — a weak label,
# because semgrep/gitleaks/... have false positives (see ADR-032). The corpus
# repos are vulnerable-by-design and DOCUMENT their planted vulns themselves;
# those docs are AUTHORED labels, higher trust than a scanner guess. This script
# reads them.
#
# Format: docs/design/authored-ground-truth-format.md  (schema keur.authored-gt/1)
# One JSON object per line:
#   {schema,repo,expect,category,cwe?,granularity,locator,label_source,
#    confidence,evidence,reviewed}
#
# Design: a per-repo DISPATCHER picks an adapter by what the repo ships. Adapters
# are deterministic parsers (sinks_json, challenge_md, vulnerable_by_design);
# free-prose repos fall back to an llm_prose stub (confidence:low, reviewed:false).
# Unknown repos are SKIPPED with a note, never guessed (park-don't-drop, ADR-029).
#
# Usage:
#   eval/gt-harvest.sh                     harvest every known repo -> the JSONL
#   eval/gt-harvest.sh <repo> [<repo>...]  harvest only the named repo(s)
#   eval/gt-harvest.sh --list              show which adapter each repo maps to
#   eval/gt-harvest.sh --stats             summarise the existing output
#
# macOS + Linux safe: no grep -P, no realpath; parsing is delegated to python3.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
CORPUS="$HERE/corpus/repos"
OUT="$HERE/corpus/authored-groundtruth.jsonl"
SCHEMA="keur.authored-gt/1"

have() { command -v "$1" >/dev/null 2>&1; }
note() { echo "gt-harvest: $*" >&2; }

# --- adapter registry: repo dir -> adapter name -----------------------------
# The mapping is data. A repo with no entry is skipped (reported by --list).
# Deterministic adapters run always. The llm_prose adapter runs ONLY when LLM
# mode is on (KEUR_LLM=1 or --llm), because it is slow and its output is a
# low-confidence, human-reviewable CANDIDATE (ADR-032), never default truth.
#
# LEGIT-UPSTREAM repos (real libraries: express/gin/flask/requests/sinatra) are
# NOT vulnerable-by-design — they get NO vuln label from llm_prose (wrong frame).
is_legit_upstream() {
    case "$1" in
        expressjs_express|gin-gonic_gin|pallets_flask|psf_requests|sinatra_sinatra) return 0 ;;
        *) return 1 ;;
    esac
}

adapter_for() {
    case "$1" in
        gbleaney_python_security)              echo "sinks_json"; return ;;
        juice-shop_juice-shop)                 echo "juice_shop"; return ;;
        OWASP_crAPI)                           echo "challenge_md"; return ;;
        bridgecrewio_terragoat|bridgecrewio_cfngoat|bridgecrewio_cdkgoat|ine-labs_AWSGoat|tenable_terrascan)
                                               echo "vulnerable_by_design"; return ;;
        we45_DVFaaS-Damn-Vulnerable-Functions-as-a-Service)
                                               echo "vuln_dir_readme"; return ;;
    esac
    # Fallback: LLM prose labeling for vulnerable-by-design repos with only prose
    # docs — gated on LLM mode, and never for legit-upstream libraries.
    if [[ "${KEUR_LLM:-0}" = "1" ]] && ! is_legit_upstream "$1"; then
        echo "llm_prose"; return
    fi
    echo ""
}

# =============================================================================
# ADAPTERS — each reads one repo's authored docs, prints common-format JSONL.
# All localisation is honest: a source that only knows a function name emits
# granularity=function, not a fabricated file/line.
# =============================================================================

# inline_marker: the STRONGEST authored ground truth — the repo author's own
# in-code vuln comments. Two shapes, both machine-parseable:
#   INTENTIONAL (RULE-ID): desc   -> the author names the exact rule that should
#                                    fire here (mestjs convention, keur-eval-style)
#   generic markers (VULNERABLE / INSECURE / "Broken Object Level ...") -> the
#                                    author flags the line as a planted vuln.
# These are confidence:high reviewed:true — the author wrote and adjudicated them.
# Runs on EVERY non-legit repo (a pre-pass), independent of the doc adapter.
adapter_inline_marker() {
    local repo="$1" root="$CORPUS/$1"
    SCHEMA="$SCHEMA" REPO="$repo" python3 - "$root" <<'PY'
import os, sys, re, json
schema=os.environ["SCHEMA"]; repo=os.environ["REPO"]; root=sys.argv[1]
CODE_EXT=(".py",".js",".ts",".tsx",".php",".java",".go",".rb",".tf",".yml",".yaml")
SKIP=("/node_modules/","/vendor/","/.git/","/dist/","/build/",".min.js","/test/","/tests/")
COMMENT=re.compile(r'(?://|#|/\*|\*|<!--)\s*(.*)')
# author intent markers (case-insensitive)
INTENT=re.compile(r'\bINTENTIONAL\s*\(([A-Za-z][A-Za-z0-9-]+)\)\s*:?\s*(.*)', re.I)
GENERIC=re.compile(r'\b(INTENTIONALLY\s+(?:VULNERABLE|INSECURE)|VULNERABLE|INSECURE USE OF|Broken (?:Object|Function|User)[- ]?Level|SSJS injection|command injection|SQL ?injection)\b', re.I)

def categorise(ruleid, text):
    s=(ruleid+" "+text).lower()
    if "sqli" in s or "sql injection" in s or "sql-injection" in s: return "sqli","CWE-89"
    if "xss" in s: return "xss","CWE-79"
    if "cmdi" in s or "command inj" in s or "os-command" in s or "ssjs" in s or "eval(" in s: return "cmdi","CWE-78"
    if "xxe" in s: return "xxe","CWE-611"
    if "ssrf" in s: return "ssrf","CWE-918"
    if "idor" in s or "bola" in s or "broken object" in s: return "idor","CWE-639"
    if "jwt" in s or "sec-016" in s: return "jwt","CWE-347"
    if "secret" in s or "sec-002" in s or "api key" in s or "password" in s: return "secret","CWE-798"
    if "csrf" in s: return "csrf","CWE-352"
    if "cookie" in s or "httponly" in s or "auth-001" in s: return "insecure-cookie","CWE-614"
    if "path" in s and "travers" in s: return "path-traversal","CWE-22"
    if "deserial" in s: return "deserialization","CWE-502"
    if "next_public" in s or "next-001" in s: return "secret","CWE-200"
    return "vuln",""

emitted=0
for dp,_,fns in os.walk(root):
    if any(sk.strip("/") in dp.split(os.sep) for sk in ("node_modules","vendor",".git","dist","build")): continue
    for fn in fns:
        if not fn.endswith(CODE_EXT): continue
        p=os.path.join(dp,fn)
        if any(sk in p for sk in SKIP): continue
        rel=os.path.relpath(p,root)
        try:
            for i,line in enumerate(open(p,encoding="utf-8",errors="ignore"),1):
                cm=COMMENT.search(line)
                if not cm: continue
                body=cm.group(1)
                m=INTENT.search(body)
                if m:
                    ruleid=m.group(1); desc=m.group(2).strip()
                    cat,cwe=categorise(ruleid, desc)
                    print(json.dumps({
                        "schema":schema,"repo":repo,"expect":"vulnerable",
                        "category":cat,"cwe":cwe,"granularity":"line",
                        "locator":{"file":rel,"line":i},
                        "label_source":"author:INTENTIONAL","expected_rule":ruleid,
                        "confidence":"high","evidence":f"author marker: INTENTIONAL ({ruleid}) {desc}"[:160],
                        "reviewed":True}))
                    emitted+=1
                elif GENERIC.search(body):
                    cat,cwe=categorise("", body)
                    if cat=="vuln": continue  # too vague to categorise → skip, don't guess
                    print(json.dumps({
                        "schema":schema,"repo":repo,"expect":"vulnerable",
                        "category":cat,"cwe":cwe,"granularity":"line",
                        "locator":{"file":rel,"line":i},
                        "label_source":"author:comment",
                        "confidence":"high","evidence":f"author marker: {body.strip()}"[:160],
                        "reviewed":True}))
                    emitted+=1
        except Exception:
            continue
sys.stderr.write(f"gt-harvest: {repo}: inline_marker emitted {emitted}\n")
PY
}

# sinks_json: a machine-readable list of vulnerable functions. FUNCTION-level.
# If a sink's function name is found in the repo source, upgrade to file/line.
# juice_shop: OWASP Juice Shop tags its OWN vulnerable lines with
#   // vuln-code-snippet vuln-line <challengeKey> [<challengeKey> ...]
# and defines each challenge's category in data/static/challenges.yml. We map the
# challenge key -> category -> keur category and emit an EXACT-LOCUS author label.
# This is high-trust: the maintainers pin the vulnerable line themselves.
adapter_juice_shop() {
    local repo="$1" root="$CORPUS/$1"
    SCHEMA="$SCHEMA" REPO="$repo" python3 - "$root" <<'PY'
import os, sys, re, json
schema=os.environ["SCHEMA"]; repo=os.environ["REPO"]; root=sys.argv[1]

# challenge key -> juice-shop category
cat_by_key={}
ymlp=os.path.join(root,"data","static","challenges.yml")
if os.path.exists(ymlp):
    cur={}
    for line in open(ymlp,encoding="utf-8",errors="ignore"):
        m=re.match(r"\s*category:\s*'?([^'\n]+)'?",line)
        if m: cur["cat"]=m.group(1).strip()
        k=re.match(r"\s*key:\s*([A-Za-z0-9_]+)",line)
        if k: cat_by_key[k.group(1)]=cur.get("cat","?"); cur={}

def keur_cat(js_cat, keys):
    s=(js_cat+" "+" ".join(keys)).lower()
    if "xss" in s: return "xss","CWE-79"
    if "sql" in s or "injection" in s and "sql" in s: return "sqli","CWE-89"
    if "nosql" in s: return "sqli","CWE-943"
    if "broken access" in s or "idor" in s or "bola" in s: return "idor","CWE-639"
    if "broken authentication" in s: return "broken-auth","CWE-287"
    if "sensitive data" in s or "data exposure" in s: return "data-exposure","CWE-200"
    if "injection" in s or "command" in s: return "cmdi","CWE-77"
    if "deserial" in s: return "deserialization","CWE-502"
    if "xxe" in s: return "xxe","CWE-611"
    if "vulnerable components" in s: return "supply-chain","CWE-1035"
    return "vuln",""

marker=re.compile(r'vuln-code-snippet\s+vuln-line\s+(.+)$')
emitted=0
for dp,_,fns in os.walk(root):
    if any(s in dp for s in ("/node_modules","/.git","/frontend/dist","/build")): continue
    for fn in fns:
        if not fn.endswith((".ts",".js")): continue
        p=os.path.join(dp,fn); rel=os.path.relpath(p,root)
        try:
            for i,line in enumerate(open(p,encoding="utf-8",errors="ignore"),1):
                m=marker.search(line)
                if not m: continue
                keys=[k for k in m.group(1).split() if re.match(r'^[A-Za-z0-9_]+$',k)]
                js_cats=[cat_by_key.get(k,"") for k in keys]
                cat,cwe=keur_cat(" ".join(c for c in js_cats if c), keys)
                if cat=="vuln": continue
                print(json.dumps({
                    "schema":schema,"repo":repo,"expect":"vulnerable",
                    "category":cat,"cwe":cwe,"granularity":"line",
                    "locator":{"file":rel,"line":i},
                    "label_source":"author:vuln-code-snippet","expected_rule":"",
                    "confidence":"high",
                    "evidence":f"juice-shop vuln-line: {', '.join(keys)} ({'/'.join(c for c in js_cats if c)})"[:160],
                    "reviewed":True}))
                emitted+=1
        except Exception: continue
sys.stderr.write(f"gt-harvest: {repo}: juice_shop emitted {emitted}\n")
PY
}

# sinks_json: a machine-readable list of vulnerable functions. FUNCTION-level.
# If a sink's function name is found in the repo source, upgrade to file/line.
adapter_sinks_json() {
    local repo="$1" root="$CORPUS/$1"
    local sinks="$root/sinks.json"
    [[ -f "$sinks" ]] || { note "$repo: no sinks.json, skip"; return; }
    SCHEMA="$SCHEMA" REPO="$repo" ROOT="$root" python3 - "$sinks" <<'PY'
import json, os, sys, re
sinks = json.load(open(sys.argv[1]))
schema = os.environ["SCHEMA"]; repo = os.environ["REPO"]; root = os.environ["ROOT"]

# Map the repo's own category names to keur's coarse categories.
def cat(name):
    n = (name or "").lower()
    if "template injection" in n or "ssti" in n: return "ssti"
    if "code execution" in n or "command" in n:  return "command-injection"
    if "deserial" in n:                          return "deserialization"
    if "sql" in n:                               return "sqli"
    return "code-execution"

# Build a small index of source files to try to localise a sink to file/line.
srcs = []
for dp, _, fns in os.walk(root):
    if any(skip in dp for skip in ("/venv", "/.git", "/node_modules")): continue
    for fn in fns:
        if fn.endswith((".py", ".js", ".ts", ".java", ".go", ".rb", ".php")):
            srcs.append(os.path.join(dp, fn))

def locate(func):
    # match the short function name (last dotted segment), as a call.
    short = func.split(".")[-1]
    if not short: return None
    pat = re.compile(r"\b" + re.escape(short) + r"\s*\(")
    for p in srcs:
        try:
            for i, line in enumerate(open(p, encoding="utf-8", errors="ignore"), 1):
                if pat.search(line):
                    return (os.path.relpath(p, root), i)
        except Exception:
            continue
    return None

for s in sinks:
    func = s.get("vulnerable_function", "")
    if not func: continue
    category = cat(s.get("category_name"))
    loc = locate(func)
    if loc:
        gran = "line"; locator = {"file": loc[0], "line": loc[1]}
        conf = "high"
    else:
        gran = "function"; locator = {"function": func}
        conf = "high"  # authored manifest is high-trust even without a locus
    rec = {
        "schema": schema, "repo": repo, "expect": "vulnerable",
        "category": category, "granularity": gran, "locator": locator,
        "label_source": "sinks.json", "confidence": conf,
        "evidence": f"authored sink: {func} ({s.get('category_name','')})".strip(),
        "reviewed": False,
    }
    if s.get("source"): rec["cwe"] = ""  # source is a URL, not a CWE; leave cwe blank
    print(json.dumps(rec))
PY
}

# challenge_md: OWASP challenge docs. VULN-CLASS level (## heading = a CWE class).
adapter_challenge_md() {
    local repo="$1" root="$CORPUS/$1"
    local doc=""
    for cand in docs/challenges.md docs/challengeSolutions.md CHALLENGES.md; do
        [[ -f "$root/$cand" ]] && { doc="$cand"; break; }
    done
    [[ -n "$doc" ]] || { note "$repo: no challenge doc, skip"; return; }
    SCHEMA="$SCHEMA" REPO="$repo" DOC="$doc" python3 - "$root/$doc" <<'PY'
import os, sys, json, re
schema = os.environ["SCHEMA"]; repo = os.environ["REPO"]; doc = os.environ["DOC"]
text = open(sys.argv[1], encoding="utf-8", errors="ignore").read()

# Map OWASP class headings to keur category + CWE.
CLASS = {
    "bola":            ("bola", "CWE-639"),
    "broken user auth":("broken-auth", "CWE-287"),
    "excessive data":  ("data-exposure", "CWE-213"),
    "rate limit":      ("rate-limit", "CWE-770"),
    "bfla":            ("bfla", "CWE-285"),
    "mass assignment": ("mass-assignment", "CWE-915"),
    "ssrf":            ("ssrf", "CWE-918"),
    "injection":       ("injection", "CWE-74"),
    "improper":        ("improper-assets", "CWE-1059"),
    "jwt":             ("jwt", "CWE-347"),
}
def classify(h):
    hl = h.lower()
    for k,(c,w) in CLASS.items():
        if k in hl: return c, w
    return "vuln-class", ""

cur_class = None
for line in text.splitlines():
    m2 = re.match(r"^##\s+(.*)", line)      # class heading
    m3 = re.match(r"^###\s+(.*)", line)     # a specific challenge
    if m2 and not line.startswith("###"):
        cur_class = m2.group(1).strip()
    elif m3 and cur_class:
        chal = m3.group(1).strip()
        cat, cwe = classify(cur_class)
        rec = {
            "schema": schema, "repo": repo, "expect": "vulnerable",
            "category": cat, "cwe": cwe, "granularity": "class",
            "locator": {"challenge": chal, "cwe": cwe},
            "label_source": doc, "confidence": "medium",
            "evidence": f"{cur_class} :: {chal}",
            "reviewed": False,
        }
        print(json.dumps(rec))
PY
}

# vuln_dir_readme: each vuln folder with a README = a directory-level label.
adapter_vuln_dir_readme() {
    local repo="$1" root="$CORPUS/$1"
    SCHEMA="$SCHEMA" REPO="$repo" python3 - "$root" <<'PY'
import os, sys, json
schema = os.environ["SCHEMA"]; repo = os.environ["REPO"]; root = sys.argv[1]
def cat(d):
    n = d.lower()
    if "xxe" in n: return "xxe", "CWE-611"
    if "inject" in n: return "injection", "CWE-74"
    if "iam" in n or "privilege" in n: return "privilege-escalation", "CWE-269"
    if "deserial" in n: return "deserialization", "CWE-502"
    return "vuln", ""
for d in sorted(os.listdir(root)):
    sub = os.path.join(root, d)
    if not os.path.isdir(sub): continue
    if not os.path.isfile(os.path.join(sub, "README.md")): continue
    if d.startswith(".") : continue
    c, w = cat(d)
    if c == "vuln": continue  # only emit folders we can categorise
    print(json.dumps({
        "schema": schema, "repo": repo, "expect": "vulnerable",
        "category": c, "cwe": w, "granularity": "directory",
        "locator": {"directory": d},
        "label_source": f"{d}/README.md", "confidence": "medium",
        "evidence": f"per-vuln folder documented: {d}",
        "reviewed": False,
    }))
PY
}

# vulnerable_by_design: IaC goats — a repo-level recall signal per file category.
adapter_vulnerable_by_design() {
    local repo="$1" root="$CORPUS/$1"
    SCHEMA="$SCHEMA" REPO="$repo" python3 - "$root" <<'PY'
import os, sys, json
schema = os.environ["SCHEMA"]; repo = os.environ["REPO"]; root = sys.argv[1]
# Emit ONE coarse repo-level label per IaC provider dir found (recall signal only).
seen = set()
for dp, _, fns in os.walk(root):
    if "/.git" in dp: continue
    for fn in fns:
        if fn.endswith((".tf", ".yaml", ".yml", ".json")) and any(
            k in dp.lower() for k in ("terraform", "cloudformation", "cdk", "k8s", "kubernetes")):
            prov = os.path.relpath(dp, root).split(os.sep)[0]
            if prov in seen: continue
            seen.add(prov)
            print(json.dumps({
                "schema": schema, "repo": repo, "expect": "vulnerable",
                "category": "iac-misconfig", "granularity": "repo",
                "locator": {"directory": prov},
                "label_source": "vulnerable-by-design (README)", "confidence": "low",
                "evidence": f"vulnerable-by-design IaC under {prov}/",
                "reviewed": False,
            }))
PY
}

# llm_prose: LLM-assisted labeling for free-prose repos (Ollama curator, ADR-032 §3).
# The repo is vulnerable-by-design but has no machine-readable manifest. We ask a
# local LLM, per source file, to locate the SINGLE most likely planted vuln and
# emit strict JSON. Output is written confidence:low reviewed:false — a CANDIDATE
# for human adjudication, NEVER auto-truth (the LLM is fallible; ADR-032).
#
# Safety caps (env-overridable):
#   KEUR_LLM_MODEL     model to use            (default keur-rule-engineer)
#   KEUR_LLM_MAXFILES  max source files/repo   (default 40)
#   KEUR_LLM_SNIPPET   bytes of file sent      (default 1400)
#   KEUR_LLM_TIMEOUT   seconds per call        (default 60)
adapter_llm_prose() {
    local repo="$1" root="$CORPUS/$1"
    have ollama || { note "$repo: ollama absent, llm_prose skipped"; return; }
    local model="${KEUR_LLM_MODEL:-keur-rule-engineer}"
    local maxfiles="${KEUR_LLM_MAXFILES:-40}"
    local snipbytes="${KEUR_LLM_SNIPPET:-1400}"
    local tmo="${KEUR_LLM_TIMEOUT:-60}"

    # candidate source files: real code, skip vendored/test/build/asset noise.
    local files; files="$(find "$root" -type f \
        \( -name '*.py' -o -name '*.js' -o -name '*.ts' -o -name '*.php' \
           -o -name '*.java' -o -name '*.go' -o -name '*.rb' -o -name '*.tf' \) 2>/dev/null \
        | grep -vE '/(\.git|node_modules|vendor|dist|build|test|tests|__tests__|\.next)/' \
        | head -n "$maxfiles")"
    [[ -n "$files" ]] || { note "$repo: no source files for llm_prose"; return; }

    local n=0
    while IFS= read -r f; do
        [[ -n "$f" ]] || continue
        n=$((n+1))
        local rel="${f#"$root"/}"
        local snip; snip="$(head -c "$snipbytes" "$f" 2>/dev/null)"
        [[ -n "$snip" ]] || continue
        local prompt="You label deliberately-vulnerable training code. This file is from a vulnerable-by-design app. Identify the SINGLE most likely planted security vulnerability in the snippet. Respond with ONE line of strict JSON only, no prose:
{\"category\":\"<sqli|xss|cmdi|xxe|ssrf|path-traversal|ssti|open-redirect|deserialization|secret|idor|csrf|none>\",\"cwe\":\"CWE-xxx or empty\",\"line\":<int line offset in snippet or 0>,\"confidence\":\"low|medium|high\",\"reason\":\"<max 12 words>\"}

SNIPPET:
$snip"
        local resp; resp="$(timeout "$tmo" ollama run --nowordwrap "$model" "$prompt" </dev/null 2>/dev/null \
            | sed $'s/\x1b\\[[0-9;]*[A-Za-z]//g')"
        SCHEMA="$SCHEMA" REPO="$repo" REL="$rel" MODEL="$model" RESP="$resp" \
            python3 - <<'PY'
import os, sys, json, re
schema=os.environ["SCHEMA"]; repo=os.environ["REPO"]; rel=os.environ["REL"]; model=os.environ["MODEL"]
raw=os.environ.get("RESP","")
m=re.search(r'\{.*\}', raw, re.S)
if not m: sys.exit(0)
try: d=json.loads(m.group(0))
except Exception: sys.exit(0)
cat=(d.get("category","") or "").strip().lower()
# normalise the model's free-form category to keur's coarse set
NORM={"sql-injection":"sqli","sql injection":"sqli","sqli":"sqli",
      "cross-site-scripting":"xss","cross site scripting":"xss","xss":"xss",
      "command-injection":"cmdi","command injection":"cmdi","os-command-injection":"cmdi","cmdi":"cmdi","rce":"cmdi",
      "xxe":"xxe","ssrf":"ssrf","path-traversal":"path-traversal","directory-traversal":"path-traversal","lfi":"path-traversal",
      "ssti":"ssti","open-redirect":"open-redirect","deserialization":"deserialization","insecure-deserialization":"deserialization",
      "secret":"secret","hardcoded-secret":"secret","idor":"idor","bola":"idor","csrf":"csrf"}
cat=NORM.get(cat, cat)
if not cat or cat=="none": sys.exit(0)
conf=(d.get("confidence","low") or "low").strip().lower()
if conf not in ("low","medium","high"): conf="low"
# LLM-sourced labels are always downgraded to low + unreviewed (ADR-032): a
# fallible curator, not truth. A human clears them to reviewed:true later.
try: line=int(d.get("line",0) or 0)
except Exception: line=0
rec={"schema":schema,"repo":repo,"expect":"vulnerable","category":cat,
     "cwe":(d.get("cwe","") or ""),
     "granularity":"line" if line>0 else "file",
     "locator":({"file":rel,"line":line} if line>0 else {"file":rel}),
     "label_source":f"llm:{model}","confidence":"low",
     "evidence":("LLM: "+(d.get("reason","") or "")[:80]).strip(),
     "reviewed":False}
print(json.dumps(rec))
PY
    done <<<"$files"
    note "$repo: llm_prose scanned $n file(s)"
}

# --- dispatch ---------------------------------------------------------------
harvest_repo() {
    local repo="$1" adapter
    [[ -d "$CORPUS/$repo" ]] || { note "$repo: not in corpus, skip"; return; }
    # Pre-pass: the author's own in-code markers (strongest GT) on every repo
    # that is not a legit upstream library.
    if ! is_legit_upstream "$repo"; then
        adapter_inline_marker "$repo"
    fi
    # Then the repo's doc-based adapter (sinks.json / challenges / prose / LLM).
    adapter="$(adapter_for "$repo")"
    if [[ -z "$adapter" ]]; then note "$repo: no doc-adapter (inline markers only)"; return; fi
    "adapter_$adapter" "$repo"
}

list_adapters() {
    for d in "$CORPUS"/*/; do
        local repo; repo="$(basename "$d")"
        printf '  %-55s %s\n' "$repo" "$(adapter_for "$repo" || true)"
    done
}

stats() {
    [[ -f "$OUT" ]] || { echo "no output yet: $OUT"; return; }
    python3 - "$OUT" <<'PY'
import json, sys, collections
byrepo=collections.Counter(); bygran=collections.Counter(); byconf=collections.Counter()
n=0
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    r=json.loads(l); n+=1
    byrepo[r["repo"]]+=1; bygran[r["granularity"]]+=1; byconf[r["confidence"]]+=1
print(f"authored ground truth: {n} labeled cases")
print("  by repo:       ", dict(byrepo))
print("  by granularity:", dict(bygran))
print("  by confidence: ", dict(byconf))
PY
}

main() {
    local append=0
    # flag pass (order-independent): --list/--stats short-circuit; --llm/--append
    local args=()
    for a in "$@"; do
        case "$a" in
            --list)   list_adapters; exit 0 ;;
            --stats)  stats; exit 0 ;;
            --llm)    export KEUR_LLM=1 ;;
            --append) append=1 ;;
            *)        args+=("$a") ;;
        esac
    done
    have python3 || { note "python3 required"; exit 1; }

    local repos=()
    if [[ ${#args[@]} -gt 0 ]]; then
        repos=("${args[@]}")
    else
        for d in "$CORPUS"/*/; do repos+=("$(basename "$d")"); done
    fi

    local tmp; tmp="$(mktemp)"
    for repo in "${repos[@]}"; do harvest_repo "$repo" >>"$tmp"; done

    if [[ "$append" = "1" && -f "$OUT" ]]; then
        cat "$tmp" >> "$OUT"; rm -f "$tmp"
    else
        mv "$tmp" "$OUT"
    fi
    note "wrote $(wc -l <"$OUT" | tr -d ' ') labeled cases -> $OUT"
}

main "$@"
