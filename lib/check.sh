#!/usr/bin/env bash
# lib/check.sh — gate the staged diff for a tier. Called by the hooks.
# Exit: 0 = PASS/WARN (allowed), 1 = BLOCK. Findings are evidence; policy decides.
set -euo pipefail
# shellcheck source=/dev/null
. "$KEUR_HOME/lib/common.sh"

TIER="pre-commit"; AI="off"
while [ $# -gt 0 ]; do
  case "$1" in
    --tier) TIER="$2"; shift 2 ;;
    --ai)   AI="$2"; shift 2 ;;
    *) shift ;;
  esac
done

CONFIG="$KEUR_HOME/config/keur.toml"
# Repo/dir overrides would be merged here in the full implementation.

# Collect the staged files (added/copied/modified), NUL-safe.
mapfile -d '' -t FILES < <(git diff --cached --name-only --diff-filter=ACM -z 2>/dev/null || true)
if [ "${#FILES[@]}" -eq 0 ]; then
  keur_ok "KEUR ($TIER): no staged files — PASS"
  exit 0
fi

keur_info "${C_DIM}KEUR ($TIER): scanning ${#FILES[@]} staged file(s)…${C_RST}"

# ── Scanner pipeline (evidence collection) ──────────────────────────────────
# Each scanner emits normalised findings to a temp JSONL. Missing tools skip
# gracefully. This scaffold wires secrets (gitleaks) + the built-in rule/pii
# engines; deps/sast are added on the pre-push tier.
FINDINGS="$(mktemp)"; trap 'rm -f "$FINDINGS"' EXIT

run_secrets() {
  keur_have gitleaks || { keur_info "${C_DIM}  secrets: gitleaks absent — skipped${C_RST}"; return 0; }
  # protect --staged is the portable form; newer gitleaks also has 'git'.
  gitleaks protect --staged --no-banner --report-format json --report-path "$FINDINGS.gl" 2>/dev/null || true
  # gitleaks always writes a report ("[]" when clean). Only count REAL findings:
  # a non-empty JSON array (contains an object).
  if [ -s "${FINDINGS}.gl" ] && grep -q '"RuleID"\|"Secret"\|"Match"' "${FINDINGS}.gl" 2>/dev/null; then
    printf '{"source":"gitleaks","category":"secret","severity":"critical","confidence":0.99,"deterministic":true,"action":"block"}\n' >> "$FINDINGS"
  fi
  rm -f "${FINDINGS}.gl"
}

run_builtin_rules() {
  # Placeholder for the built-in rule/pii engine (rules/**/*.rule).
  # The engine will emit normalised Finding JSON lines here.
  [ -x "$KEUR_HOME/lib/rules.sh" ] && "$KEUR_HOME/lib/rules.sh" "${FILES[@]}" >> "$FINDINGS" || true
}

run_secrets
run_builtin_rules

# ── Policy engine (decision) ────────────────────────────────────────────────
BLOCK_AT="$(keur_toml "$CONFIG" policy block-at)"; BLOCK_AT="${BLOCK_AT:-0.85}"
verdict="PASS"
if grep -q '"action":"block"' "$FINDINGS" 2>/dev/null; then
  verdict="BLOCK"
elif [ -s "$FINDINGS" ]; then
  verdict="WARN"
fi

case "$verdict" in
  BLOCK) keur_error "KEUR ($TIER): BLOCK — blocking finding(s) detected"; exit 1 ;;
  WARN)  keur_warn  "KEUR ($TIER): WARN — non-blocking finding(s); review advised"; exit 0 ;;
  PASS)  keur_ok    "KEUR ($TIER): PASS"; exit 0 ;;
esac
