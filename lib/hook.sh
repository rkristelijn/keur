#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# lib/hook.sh — install/manage KEUR git hooks (per-repo and global).
set -euo pipefail
# shellcheck source=/dev/null
. "$KEUR_HOME/lib/common.sh"

TEMPLATES="$KEUR_HOME/hooks/templates"
GLOBAL_HOOKS_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/git/hooks"

install_into() {
  # install_into <hooks-dir>
  local dir="$1"
  mkdir -p "$dir"
  for hook in pre-commit pre-push; do
    cp "$TEMPLATES/$hook" "$dir/$hook"
    chmod +x "$dir/$hook"
    keur_ok "  installed $hook → $dir/$hook"
  done
}

case "${1:-}" in
  --install)
    repo_hooks="$(git rev-parse --git-path hooks 2>/dev/null || true)"
    [ -n "$repo_hooks" ] || { keur_error "not inside a git repo"; exit 1; }
    install_into "$repo_hooks"
    keur_info "Per-repo KEUR hooks installed. They run 'keur check' on commit/push."
    ;;
  --global)
    install_into "$GLOBAL_HOOKS_DIR"
    # Point git at the global hooks dir (respect an existing custom path).
    current="$(git config --global core.hooksPath || true)"
    if [ -n "$current" ] && [ "$current" != "$GLOBAL_HOOKS_DIR" ]; then
      keur_warn "core.hooksPath already set to: $current"
      keur_warn "Leaving it. Merge KEUR hooks into that dir, or unset to use $GLOBAL_HOOKS_DIR."
    else
      git config --global core.hooksPath "$GLOBAL_HOOKS_DIR"
      keur_ok "  git core.hooksPath → $GLOBAL_HOOKS_DIR"
    fi
    keur_info "Global KEUR hooks installed for all repos on this machine."
    ;;
  --status)
    keur_info "Global core.hooksPath: $(git config --global core.hooksPath || echo '(unset)')"
    keur_info "Global hooks dir:      $GLOBAL_HOOKS_DIR"
    for h in pre-commit pre-push; do
      [ -x "$GLOBAL_HOOKS_DIR/$h" ] && keur_ok "  global $h: installed" || keur_info "  global $h: (absent)"
    done
    repo_hooks="$(git rev-parse --git-path hooks 2>/dev/null || true)"
    if [ -n "$repo_hooks" ]; then
      for h in pre-commit pre-push; do
        [ -x "$repo_hooks/$h" ] && keur_ok "  repo $h: installed" || keur_info "  repo $h: (absent)"
      done
    fi
    ;;
  --remove)
    if [ "${2:-}" = "--global" ]; then
      for h in pre-commit pre-push; do rm -f "$GLOBAL_HOOKS_DIR/$h" && keur_info "  removed $GLOBAL_HOOKS_DIR/$h"; done
      keur_warn "Left core.hooksPath as-is. Unset with: git config --global --unset core.hooksPath"
    else
      repo_hooks="$(git rev-parse --git-path hooks 2>/dev/null || true)"
      [ -n "$repo_hooks" ] || { keur_error "not inside a git repo"; exit 1; }
      for h in pre-commit pre-push; do rm -f "$repo_hooks/$h" && keur_info "  removed $repo_hooks/$h"; done
    fi
    ;;
  *)
    keur_error "usage: keur hook [--install|--global|--status|--remove [--global]]"; exit 1 ;;
esac
