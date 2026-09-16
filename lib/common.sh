#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# lib/common.sh — shared helpers. Sourced by the keur subcommands.
# Platform-safe: no grep -P, no realpath. Works on macOS + Linux/WSL.

# Colours (disabled when not a tty).
if [ -t 1 ]; then
  C_RED=$'\033[31m'; C_YEL=$'\033[33m'; C_GRN=$'\033[32m'; C_DIM=$'\033[90m'; C_RST=$'\033[0m'
else
  C_RED=""; C_YEL=""; C_GRN=""; C_DIM=""; C_RST=""
fi

keur_info()  { printf '%s\n' "$*"; }
keur_warn()  { printf '%s%s%s\n' "$C_YEL" "$*" "$C_RST" >&2; }
keur_error() { printf '%s%s%s\n' "$C_RED" "$*" "$C_RST" >&2; }
keur_ok()    { printf '%s%s%s\n' "$C_GRN" "$*" "$C_RST"; }

# Minimal TOML value reader: keur_toml <file> <section> <key>.
# Good enough for flat "key = value" lookups within a [section]. Not a full parser;
# the future native engine will replace this. Returns empty if not found.
keur_toml() {
  local file="$1" section="$2" key="$3"
  [ -f "$file" ] || return 0
  awk -v sect="$section" -v k="$key" '
    /^\[/ { in_sect = ($0 == "[" sect "]") }
    in_sect && $0 ~ "^[[:space:]]*" k "[[:space:]]*=" {
      sub(/^[^=]*=[[:space:]]*/, ""); sub(/[[:space:]]*(#.*)?$/, "");
      gsub(/^"|"$/, ""); print; exit
    }
  ' "$file"
}

# Is a tool available on PATH?
keur_have() { command -v "$1" >/dev/null 2>&1; }

# Current branch, empty if not in a repo.
keur_branch() { git rev-parse --abbrev-ref HEAD 2>/dev/null || true; }
