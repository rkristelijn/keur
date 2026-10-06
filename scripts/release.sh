#!/usr/bin/env bash
# release.sh — deterministic version bump + release notes from Conventional Commits.
#
# The release logic lives here (a script), not in the workflow — the workflow is
# a thin trigger (ADR-044: make/CI orchestrate, scripts implement). Pure-ish:
# reads git history + config/keur.toml, writes keur.toml + CHANGELOG.md on apply.
#
# Subcommands:
#   bump              Print the next semver (computed from commits since the last
#                     tag). Prints nothing + exits 3 when there is nothing to
#                     release (no releasable commits).
#   notes [VERSION]   Print Markdown release notes for the commits since the last
#                     tag, grouped by type.
#   apply  VERSION    Write VERSION into config/keur.toml and prepend a dated
#                     section to CHANGELOG.md (created if absent).
#   current           Print the current version from config/keur.toml.
#
# Versioning (SemVer from Conventional Commits since the last vX.Y.Z tag):
#   - any `feat!`/`fix!`/`BREAKING CHANGE` → major
#   - any `feat:`                          → minor
#   - any `fix:`/`perf:`                   → patch
#   - only chore/docs/refactor/test/ci     → patch (still a release if asked)
#   - no commits at all since the tag      → nothing to release (exit 3)
# First release (no tags): starts from the version in keur.toml (0.1.0).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
TOML="$ROOT/config/keur.toml"
CHANGELOG="$ROOT/CHANGELOG.md"

current_version() {
  grep -E '^version' "$TOML" | head -1 | sed 's/.*=[[:space:]]*//; s/"//g; s/[[:space:]]//g'
}

last_tag() { git -C "$ROOT" describe --tags --abbrev=0 --match 'v*' 2>/dev/null || true; }

# Commit subjects since the last tag (or all, for the first release).
commits_since_tag() {
  local tag; tag="$(last_tag)"
  if [ -n "$tag" ]; then
    git -C "$ROOT" log --no-merges --pretty=format:'%s%n%b' "${tag}..HEAD"
  else
    git -C "$ROOT" log --no-merges --pretty=format:'%s%n%b'
  fi
}

# Decide major|minor|patch|none from the commit messages.
bump_kind() {
  local log; log="$(commits_since_tag)"
  [ -n "$(printf '%s' "$log" | tr -d '[:space:]')" ] || { echo none; return; }
  if printf '%s\n' "$log" | grep -qE '(^|[[:space:]])(feat|fix|refactor|perf|chore|build)(\([^)]*\))?!:' \
     || printf '%s\n' "$log" | grep -qE 'BREAKING[ -]CHANGE'; then
    echo major; return
  fi
  if printf '%s\n' "$log" | grep -qE '^feat(\([^)]*\))?:'; then echo minor; return; fi
  if printf '%s\n' "$log" | grep -qE '^(fix|perf)(\([^)]*\))?:'; then echo patch; return; fi
  echo patch  # docs/chore/refactor/test/ci only — still releasable on request
}

next_version() {
  local cur kind major minor patch
  cur="$(current_version)"
  kind="$(bump_kind)"
  IFS=. read -r major minor patch <<< "$cur"
  : "${major:=0}" "${minor:=0}" "${patch:=0}"

  # First release (no tag yet): ship the current keur.toml version as-is.
  if [ -z "$(last_tag)" ]; then echo "$cur"; return; fi

  case "$kind" in
    none)  return 3 ;;
    major) echo "$((major+1)).0.0" ;;
    minor) echo "${major}.$((minor+1)).0" ;;
    patch) echo "${major}.${minor}.$((patch+1))" ;;
  esac
}

render_notes() {
  local version="${1:-$(current_version)}" tag; tag="$(last_tag)"
  local range_desc; range_desc="${tag:+since $tag}"; range_desc="${range_desc:-first release}"
  echo "## v${version} ($(date +%Y-%m-%d))"
  echo ""
  echo "_Changes ${range_desc}._"
  echo ""
  local types=("feat:Features" "fix:Fixes" "perf:Performance" "refactor:Refactoring" "docs:Documentation" "build:Build" "ci:CI" "test:Tests" "chore:Chores")
  local log; log="$(commits_since_tag)"
  for pair in "${types[@]}"; do
    local key="${pair%%:*}" label="${pair#*:}" lines
    lines="$(printf '%s\n' "$log" | grep -E "^${key}(\([^)]*\))?!?:" | sed -E "s/^${key}(\([^)]*\))?!?:[[:space:]]*//" || true)"
    if [ -n "$lines" ]; then
      echo "### ${label}"
      printf '%s\n' "$lines" | sed 's/^/- /'
      echo ""
    fi
  done
}

apply_version() {
  local version="$1"
  [ -n "$version" ] || { echo "apply: VERSION required" >&2; exit 1; }
  # 1. keur.toml
  if [[ "$OSTYPE" == darwin* ]]; then
    sed -i '' -E "s/^(version[[:space:]]*=[[:space:]]*).*/\1\"${version}\"/" "$TOML"
  else
    sed -i -E "s/^(version[[:space:]]*=[[:space:]]*).*/\1\"${version}\"/" "$TOML"
  fi
  # 2. CHANGELOG.md — prepend the new section.
  local notes; notes="$(render_notes "$version")"
  if [ -f "$CHANGELOG" ]; then
    printf '%s\n\n%s' "$notes" "$(cat "$CHANGELOG")" > "$CHANGELOG"
  else
    printf '# Changelog\n\n%s' "$notes" > "$CHANGELOG"
  fi
  echo "applied v${version} → config/keur.toml + CHANGELOG.md"
}

case "${1:-}" in
  bump)
    v="$(next_version)" || { echo "nothing to release" >&2; exit 3; }
    echo "$v" ;;
  notes)   render_notes "${2:-}" ;;
  apply)   apply_version "${2:-}" ;;
  current) current_version ;;
  *) echo "usage: release.sh {bump|notes [VERSION]|apply VERSION|current}" >&2; exit 1 ;;
esac
