#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# lib/scan.sh — run the deterministic scanner pipeline over a path (not just the
# staged diff). Full-repo/ad-hoc scanning; shares the scanner adapters with check.sh.
set -euo pipefail
# shellcheck source=/dev/null
. "$KEUR_HOME/lib/common.sh"

TARGET="${1:-.}"
keur_info "KEUR scan: $TARGET"
keur_warn "scan pipeline is a scaffold — scanner adapters land next (see docs/design/architecture.md)."
# Roadmap: iterate files under TARGET, run each configured scanner adapter,
# normalise to Findings, aggregate + calibrate, print a report (+ optional SARIF).
