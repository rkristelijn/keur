#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# lib/policy.sh — inspect the resolved policy.
set -euo pipefail
# shellcheck source=/dev/null
. "$KEUR_HOME/lib/common.sh"
CONFIG="$KEUR_HOME/config/keur.toml"

case "${1:-explain}" in
  explain)
    keur_info "Resolved policy (from $CONFIG):"
    keur_info "  block-at            = $(keur_toml "$CONFIG" policy block-at)"
    keur_info "  warn-at             = $(keur_toml "$CONFIG" policy warn-at)"
    keur_info "  corroboration-bonus = $(keur_toml "$CONFIG" policy corroboration-bonus)"
    keur_info "  block-categories    = $(keur_toml "$CONFIG" policy block-categories)"
    keur_info ""
    keur_info "Findings are evidence; this policy turns evidence into PASS/WARN/BLOCK."
    keur_info "AI (if enabled) only adds evidence — it never sets the verdict."
    ;;
  *) keur_error "usage: keur policy explain"; exit 1 ;;
esac
