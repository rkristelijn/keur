#!/usr/bin/env bash
# DEPRECATED (ADR-002): superseded by the compiled binary bin/keur. Reference only.
# lib/rules.sh — KEUR built-in rule engine (data-driven, no external tooling).
#
# Reads .rule files (KEUR's own rules/ AND, optionally, an imported cpm rules
# dir) and evaluates them against the given files. Emits one normalised Finding
# JSON object per match on stdout, which check.sh appends to the evidence file.
#
# This is the piece that makes "config as data" real: detection lives in .rule
# data files, not in code. The format is deliberately cpm-compatible so cpm's
# rule corpus can be reused as-is.
#
# Supported engines (roadmap M4):
#   pattern  — regex match on file content, with target filtering   [THIS FILE]
#   presence — a required regex must be present   (M4b, not yet)
#   absence  — a forbidden regex must be absent    (M4b, not yet)
#
# Rule schema (YAML-ish, flat enough for a bash/awk reader):
#   id, title, category, severity, engine, action (optional), confidence (opt)
#   target: { extensions, filenames, exclude_paths, content_contains }
#   patterns: [ { regex, message } ]
#   fix: "..."
#
# Platform-safe: no grep -P, no realpath, no yq. Works on macOS + Linux/WSL.
set -uo pipefail
# shellcheck source=/dev/null
. "$KEUR_HOME/lib/common.sh"

# Rule directories: KEUR's own, plus an optional imported cpm snapshot/dir.
# KEUR_RULES_DIRS can be set (colon-separated) to add external rule roots,
# e.g. KEUR_RULES_DIRS="$HOME/git/hub/cpm/rules".
RULE_ROOTS=("$KEUR_HOME/rules")
if [ -n "${KEUR_RULES_DIRS:-}" ]; then
  IFS=':' read -r -a _extra <<< "$KEUR_RULES_DIRS"
  RULE_ROOTS+=("${_extra[@]}")
fi

FILES=("$@")
[ "${#FILES[@]}" -eq 0 ] && exit 0

# ── matcher selection: ripgrep (PCRE2, fast) with a perl fallback ────────────
# cpm rules are PCRE (\s, (?:...), .{1,15}). ripgrep --pcre2 is the same engine
# cpm uses and is 10-100x faster than a per-file perl loop because it scans all
# candidate files in one process with a JIT-compiled regex. When rg is absent we
# fall back to perl, which also speaks PCRE and is present on macOS + Linux.
# Never grep -P (not on macOS) or grep -E (ERE can't parse these regexes).
if command -v rg >/dev/null 2>&1 && rg --pcre2-version >/dev/null 2>&1; then
  KEUR_MATCHER="rg"
else
  KEUR_MATCHER="perl"
fi

# match_regex <regex> <file...> → prints "file:lineno:linetext" for each match.
# One process for the whole file set (rg) instead of one per file.
match_regex() {
  local regex="$1"; shift
  if [ "$KEUR_MATCHER" = "rg" ]; then
    # -P PCRE2, -n line numbers, -H with filename, --no-heading flat output,
    # -e so a regex starting with '-' is not read as a flag.
    rg -P -n -H --no-heading --no-messages -e "$regex" -- "$@" 2>/dev/null
  else
    local f
    for f in "$@"; do
      [ -f "$f" ] || continue
      KEUR_RE="$regex" perl -ne 'print "'"$f"':$.:$_" if /$ENV{KEUR_RE}/' "$f" 2>/dev/null
    done
  fi
}

# ── tiny YAML field readers (flat keys + one nested block we care about) ─────
# Read a top-level scalar: rule_scalar <file> <key>
rule_scalar() {
  awk -v k="$2" '
    $0 ~ "^" k "[[:space:]]*:" {
      sub(/^[^:]*:[[:space:]]*/, ""); sub(/[[:space:]]*$/, "");
      gsub(/^"|"$/, ""); print; exit
    }' "$1"
}
# Read a nested value under target: rule_target <file> <subkey>
rule_target() {
  awk -v k="$2" '
    /^target:/ { in_t=1; next }
    /^[^[:space:]]/ { in_t=0 }
    in_t && $0 ~ "^[[:space:]]+" k "[[:space:]]*:" {
      sub(/^[^:]*:[[:space:]]*/, ""); sub(/[[:space:]]*$/, ""); print; exit
    }' "$1"
}

# Map cpm severity → KEUR action when a rule has no explicit action field.
# error → block, warning → warn, info/anything → advisory.
action_for() {
  local action="$1" severity="$2"
  if [ -n "$action" ]; then printf '%s' "$action"; return; fi
  case "$severity" in
    error|critical) printf 'block' ;;
    warning)        printf 'warn' ;;
    *)              printf 'advisory' ;;
  esac
}

confidence_for() {
  local c="$1" action="$2"
  if [ -n "$c" ]; then printf '%s' "$c"; return; fi
  case "$action" in block) printf '0.9' ;; warn) printf '0.6' ;; *) printf '0.4' ;; esac
}

# JSON-escape a string (minimal: quotes, backslashes, control chars → space).
json_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n\t' '  '; }

# Does file match this rule's target filters?
file_matches_target() {
  local f="$1" exts="$2" names="$3" excludes="$4" contains="$5"
  # exclude_paths: any token that appears in the path → skip
  if [ -n "$excludes" ]; then
    local ex; for ex in $excludes; do case "$f" in *"$ex"*) return 1 ;; esac; done
  fi
  # filenames: match basename exactly against any token
  if [ -n "$names" ]; then
    local base; base="$(basename "$f")"; local n hit=1
    for n in $names; do [ "$base" = "$n" ] && hit=0; done
    [ "$hit" -eq 0 ] || return 1
  fi
  # extensions: file must end with one of the tokens
  if [ -n "$exts" ]; then
    local e hit=1
    for e in $exts; do case "$f" in *"$e") hit=0 ;; esac; done
    [ "$hit" -eq 0 ] || return 1
  fi
  # content_contains: cheap pre-filter — file must contain this literal
  if [ -n "$contains" ]; then
    grep -qF "$contains" "$f" 2>/dev/null || return 1
  fi
  return 0
}

# Extract the regex + message pairs from a rule's patterns: list.
# Emits "regex<TAB>message" lines.
rule_patterns() {
  awk '
    /^patterns:/ { in_p=1; next }
    /^[^[:space:]]/ && !/^patterns:/ { in_p=0 }
    in_p && /^[[:space:]]*-[[:space:]]*regex:/ {
      line=$0; sub(/^[[:space:]]*-[[:space:]]*regex:[[:space:]]*/, "", line);
      regex=line; msg=""; getline nxt;
      if (nxt ~ /^[[:space:]]+message:/) { m=nxt; sub(/^[[:space:]]*message:[[:space:]]*/, "", m); gsub(/^"|"$/, "", m); msg=m }
      gsub(/\t/, " ", regex); printf "%s\t%s\n", regex, msg
    }' "$1"
}

# ── evaluate one pattern rule against the candidate files ────────────────────
eval_pattern_rule() {
  local rf="$1"
  local id title category severity action confidence
  id="$(rule_scalar "$rf" id)"
  title="$(rule_scalar "$rf" title)"
  category="$(rule_scalar "$rf" category)"
  severity="$(rule_scalar "$rf" severity)"
  action="$(action_for "$(rule_scalar "$rf" action)" "$severity")"
  confidence="$(confidence_for "$(rule_scalar "$rf" confidence)" "$action")"

  local exts names excludes contains
  exts="$(rule_target "$rf" extensions)"
  names="$(rule_target "$rf" filenames)"
  excludes="$(rule_target "$rf" exclude_paths)"
  contains="$(rule_target "$rf" content_contains)"

  # Filter the candidate files by this rule's target once, up front.
  local -a candidates=()
  local f
  for f in "${FILES[@]}"; do
    [ -f "$f" ] || continue
    file_matches_target "$f" "$exts" "$names" "$excludes" "$contains" || continue
    candidates+=("$f")
  done
  [ "${#candidates[@]}" -eq 0 ] && return 0

  # Run each pattern once over the whole candidate set (one rg process).
  local regex message
  while IFS=$'\t' read -r regex message; do
    [ -n "$regex" ] || continue
    local line
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      # line = "file:lineno:linetext"
      local file lineno linetext rest
      file="${line%%:*}"; rest="${line#*:}"
      lineno="${rest%%:*}"; linetext="${rest#*:}"

      # Inline suppression (T-207): keur:ignore [<RULE-ID>] on the offending line.
      case "$linetext" in
        *"keur:ignore $id"*|*"keur:ignore"*) continue ;;
      esac

      printf '{"source":"keur-rules","rule":"%s","title":"%s","category":"%s","severity":"%s","action":"%s","confidence":%s,"deterministic":true,"file":"%s","line":%s,"message":"%s"}\n' \
        "$(json_escape "$id")" "$(json_escape "$title")" "$(json_escape "$category")" \
        "$(json_escape "$severity")" "$action" "$confidence" \
        "$(json_escape "$file")" "$lineno" "$(json_escape "${message:-$title}")"
    done < <(match_regex "$regex" "${candidates[@]}")
  done < <(rule_patterns "$rf")
}

# ── main: iterate all .rule files, dispatch by engine ────────────────────────
for root in "${RULE_ROOTS[@]}"; do
  [ -d "$root" ] || continue
  # find is fine here; we only read files.
  while IFS= read -r rf; do
    engine="$(rule_scalar "$rf" engine)"
    case "$engine" in
      pattern) eval_pattern_rule "$rf" ;;
      presence|absence) : ;;   # M4b — not yet implemented
      *) : ;;
    esac
  done < <(find "$root" -type f -name '*.rule' 2>/dev/null)
done
