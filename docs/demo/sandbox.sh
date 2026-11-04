#!/usr/bin/env bash
# Strict mode up top. Compact form satisfies both KEUR-QUAL-014
# (e+pipefail on one line) and SH-QUAL-010 (finds -e/-u/pipefail).
set -euo pipefail
# sandbox.sh — run keur in a hermetic, machine-independent environment.
# @shows ADR-047
#
# WHY: keur layers rules/config from the machine — global ~/.config/keur/keur.toml,
# the personal ~/.keur/rules vault (PII/BII denylist), and the project .keur/rules
# (resolved from the CWD). Great for daily use, but it makes a demo NON-reproducible:
# what you record depends on YOUR machine. A colleague rendering the same tape gets
# different findings (your VAULT-GENERIC block won't fire for them, your config
# overrides won't apply).
#
# This simulates isolation with NO code change (keur is already isolatable — see
# docs/demo/sandbox.md): a clean env (env -i), empty HOME/XDG pointing at throwaway
# temp dirs so the global + personal rule layers resolve to nothing, KEUR_HOME at
# the repo so ONLY the shipped repo rules load, and CWD outside any repo with
# .keur/rules so the project layer is controlled too.
#
# Usage:
#   docs/demo/sandbox.sh keur scan <path>        # run any keur command, isolated
#   docs/demo/sandbox.sh keur check
#   KEUR_BIN=/path/to/keur docs/demo/sandbox.sh keur version
#
# The target path you pass is used as-is; run from a scratch dir you control.


HERE="$(cd "$(dirname "$0")" && pwd -P)"
REPO_ROOT="$(cd "$HERE/../.." && pwd -P)"   # docs/demo -> repo root (holds rules/)

# keur binary: $KEUR_BIN, else repo bin/, else PATH.
KEUR_BIN="${KEUR_BIN:-}"
if [[ -z "$KEUR_BIN" ]]; then
    # Prefer the INSTALLED keur on PATH (the real release a user has) over a
    # possibly-stale repo bin/keur — a demo must show the shipped version, and an
    # old local build can miss newer rule engines. Override with KEUR_BIN.
    KEUR_BIN="$(command -v keur || true)"
    [[ -z "$KEUR_BIN" && -x "$REPO_ROOT/bin/keur" ]] && KEUR_BIN="$REPO_ROOT/bin/keur"
fi
[[ -n "$KEUR_BIN" && -x "$KEUR_BIN" ]] || { echo "sandbox: no keur binary (set KEUR_BIN)" >&2; exit 2; }

# Throwaway clean home so global (~/.config/keur) and personal (~/.keur/rules)
# layers resolve to empty, non-existent dirs -> no machine rules leak in.
CLEAN="$(mktemp -d)"
trap 'rm -rf "$CLEAN"' EXIT
mkdir -p "$CLEAN/.config" "$CLEAN/.cache"

# A minimal PATH so externally-invoked scanners / git still resolve, but nothing
# from the user's shell. Keep a couple of common bin dirs.
SANDBOX_PATH="/usr/bin:/bin:/usr/sbin:/sbin"
for extra in /opt/homebrew/bin /usr/local/bin; do
    [[ -d "$extra" ]] && SANDBOX_PATH="$extra:$SANDBOX_PATH"
done

# env -i wipes the environment; we re-add ONLY the controlled set.
#   HOME/XDG_*  -> clean temp (kills global + personal rule/config layers)
#   KEUR_HOME   -> repo (loads ONLY the shipped repo rules)
#   (KEUR_RULES_DIRS intentionally UNSET; pass extra demo rules via the arg path
#    or set it yourself before calling if a demo needs a self-contained ruleset)
#   NO_COLOR left unset so demos keep colour; set KEUR_SANDBOX_NOCOLOR=1 to force off.
EXTRA=()
[[ -n "${KEUR_RULES_DIRS:-}" ]] && EXTRA+=("KEUR_RULES_DIRS=$KEUR_RULES_DIRS")
[[ -n "${KEUR_SANDBOX_NOCOLOR:-}" ]] && EXTRA+=("NO_COLOR=1")

exec env -i \
    PATH="$SANDBOX_PATH" \
    HOME="$CLEAN" \
    XDG_CONFIG_HOME="$CLEAN/.config" \
    XDG_CACHE_HOME="$CLEAN/.cache" \
    KEUR_HOME="$REPO_ROOT" \
    "${EXTRA[@]}" \
    "$KEUR_BIN" "${@:2}"
