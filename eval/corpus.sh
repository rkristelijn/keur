#!/usr/bin/env bash
# eval/corpus.sh — build a local corpus of real git repositories to train
# keur's rules against a proven-tool ground truth.
#
# The corpus is the raw material: real code with real (and realistically absent)
# security issues. Proven tools (semgrep, gitleaks, ...) produce the ground
# truth; keur's rule engine is then trained to approach that baseline.
#
# Usage:
#   eval/corpus.sh clone [N]     clone the first N repos from the list (default: all)
#   eval/corpus.sh list          show the repo list
#   eval/corpus.sh status        show what is cloned + sizes
#   eval/corpus.sh clean         remove the clone cache
#
# Repos are shallow-cloned (--depth 1) into eval/corpus/repos/ which is
# gitignored (large, reproducible from this list).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
# Cache + list default to the in-repo corpus, but can point elsewhere so an
# alternate corpus (e.g. ~/git/hub/keur-eval-3) lives fully outside this repo
# without touching the default one.
CACHE="${KEUR_CORPUS_DIR:-$HERE/corpus/repos}"
LIST="${KEUR_CORPUS_LIST:-$HERE/corpus/repos.txt}"

# --- resource guards --------------------------------------------------------
# Skip a repo whose working tree exceeds this after clone (keeps one outlier
# from filling the disk). Override with KEUR_CORPUS_MAX_MB.
MAX_REPO_MB="${KEUR_CORPUS_MAX_MB:-100}"
# Refuse to clone when free disk drops below this (MB). Override with
# KEUR_CORPUS_MIN_FREE_MB.
MIN_FREE_MB="${KEUR_CORPUS_MIN_FREE_MB:-5000}"

free_mb() { df -m "$1" 2>/dev/null | awk 'NR==2{print $4}'; }
dir_mb()  { du -sm "$1" 2>/dev/null | awk '{print $1}'; }

# ---------------------------------------------------------------------------
# Repo list: diverse, popular, security-relevant. Kept small and shallow so a
# first run is minutes not hours. Format: one "owner/name" per line (GitHub).
# Chosen for a mix of languages and known secret/injection/config surface.
# ---------------------------------------------------------------------------
default_list() {
    cat <<'REPOS'
OWASP/NodeGoat
OWASP/railsgoat
digininja/DVWA
juice-shop/juice-shop
bkimminich/juice-shop-ctf
appsecco/dvna
snyk-labs/nodejs-goof
we45/DVFaaS-Damn-Vulnerable-Functions-as-a-Service
payloadbox/sql-injection-payload-list
swisskyrepo/PayloadsAllTheThings
REPOS
}

mkdir -p "$(dirname "$LIST")"
[ -f "$LIST" ] || default_list > "$LIST"

cmd="${1:-status}"
case "$cmd" in
  list)
    cat "$LIST"
    ;;

  clone)
    n="${2:-0}"   # 0 = all
    mkdir -p "$CACHE"
    i=0
    while IFS= read -r repo; do
      [ -z "$repo" ] && continue
      case "$repo" in \#*) continue ;; esac
      i=$((i+1))
      [ "$n" -gt 0 ] && [ "$i" -gt "$n" ] && break
      name="$(echo "$repo" | tr '/' '__')"
      dest="$CACHE/$name"
      if [ -d "$dest/.git" ]; then
        echo "  have  $repo"
        continue
      fi
      # disk guard: stop before we run the volume dry.
      free="$(free_mb "$CACHE")"
      if [ -n "$free" ] && [ "$free" -lt "$MIN_FREE_MB" ]; then
        echo "  STOP  free disk ${free}MB < ${MIN_FREE_MB}MB threshold; aborting clone" >&2
        break
      fi
      echo "  clone $repo ..."
      # SSH clone: uses your configured SSH key, never prompts the macOS
      # keychain for credentials (as an HTTPS clone would). Set KEUR_CLONE_PROTO
      # to "https" if you prefer HTTPS on a machine without an SSH key.
      case "${KEUR_CLONE_PROTO:-ssh}" in
        https) url="https://github.com/$repo.git" ;;
        *)     url="git@github.com:$repo.git" ;;
      esac
      # GIT_TERMINAL_PROMPT=0 + BatchMode: fail fast instead of hanging on a prompt.
      if GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND="ssh -o BatchMode=yes" \
         git clone --depth 1 --single-branch --quiet "$url" "$dest" 2>/dev/null; then
        # size cap: drop an oversized outlier so it cannot fill the disk.
        sz="$(dir_mb "$dest")"
        if [ -n "$sz" ] && [ "$sz" -gt "$MAX_REPO_MB" ]; then
          echo "  SKIP  $repo (${sz}MB > ${MAX_REPO_MB}MB cap) — removed"
          rm -rf "$dest"
        else
          echo "  ok    $repo (${sz}MB)"
        fi
      else
        echo "  FAIL  $repo (skipped)"
        rm -rf "$dest"
      fi
    done < "$LIST"
    echo "corpus: $(find "$CACHE" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | wc -l | tr -d ' ') repo(s), $(du -sh "$CACHE" 2>/dev/null | awk '{print $1}') in $CACHE"
    ;;

  status)
    if [ ! -d "$CACHE" ]; then echo "corpus: not built (run: eval/corpus.sh clone)"; exit 0; fi
    count="$(find "$CACHE" -maxdepth 1 -mindepth 1 -type d | wc -l | tr -d ' ')"
    size="$(du -sh "$CACHE" 2>/dev/null | awk '{print $1}')"
    echo "corpus: $count repo(s), $size in $CACHE"
    find "$CACHE" -maxdepth 1 -mindepth 1 -type d -exec basename {} \; | sed 's/^/  /'
    ;;

  clean)
    rm -rf "$CACHE"
    echo "removed $CACHE"
    ;;

  *)
    echo "usage: eval/corpus.sh {clone [N]|list|status|clean}"; exit 1 ;;
esac
