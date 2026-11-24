#!/usr/bin/env bash
# test_action.sh — e2e: the install.sh wrapper contract that the composite
# Action (action.yml, ADR-062) depends on.
#
# The other e2e tests take an already-built binary and exercise a subcommand.
# This one is different on purpose: it proves the WRAPPER the CI Action runs —
# install.sh fetching a checksum-verified release binary into KEUR_BIN_DIR and
# that binary then being runnable from PATH. action.yml does exactly this
# (run install.sh → put KEUR_BIN_DIR on PATH → run `keur <args>`), so if this
# contract holds, the Action's steps hold; the only thing left for real CI is
# GitHub's `uses: ./` resolution itself.
#
# It therefore IGNORES the $1 binary argument and builds a fresh install into a
# temp dir, exactly as a consumer's runner would. Requires network + a public
# release (the repo is public with downloadable assets, ADR-045/062).
#
# Skips cleanly (exit 0) when offline or no downloader is present, so a
# sandboxed run does not report a false failure.
source "$(dirname "$0")/helpers.sh"
set +o errexit

ROOT="$(repo_root)"
INSTALL="$ROOT/install.sh"
RULES="$ROOT/rules"

echo "==> test_action (install.sh wrapper contract)"

# Preconditions: a downloader and reachable release assets. If either is
# missing (offline sandbox), skip rather than fail — this test needs the net.
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
  echo "  skip  no curl/wget — cannot exercise the fetch wrapper"
  summary; exit $?
fi
if ! curl -fsI "https://github.com/rkristelijn/keur/releases/latest" >/dev/null 2>&1; then
  echo "  skip  release endpoint unreachable (offline?) — wrapper fetch not testable here"
  summary; exit $?
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export KEUR_BIN_DIR="$TMP/.keur-bin"
export KEUR_VERSION="${KEUR_VERSION:-latest}"
mkdir -p "$KEUR_BIN_DIR"

# 1. install.sh fetches + checksum-verifies the release binary (the SCA-030
#    verify-before-trust step lives inside install.sh; a mismatch exits non-zero).
INSTALL_LOG="$TMP/install.log"
if sh "$INSTALL" >"$INSTALL_LOG" 2>&1 && [[ -x "$KEUR_BIN_DIR/keur" ]]; then
  ok "install.sh fetched + checksum-verified a keur binary"
else
  fail "install.sh did not produce a keur binary in KEUR_BIN_DIR"
  summary; exit $?
fi

# Known defect, issue #36: releases up to and including 0.2.0 link RE2
# dynamically, so the published binary aborts at load time (dyld "Library not
# loaded", Abort trap 6) on a runner without that exact library. That is a
# property of the old artifact, not of this checkout. Skip ONLY for those
# releases: once a statically linked release is published, the skip no longer
# applies and a binary that cannot start fails this test again.
PRE_STATIC_RELEASES="0.1.0 0.2.0"
installed="$(sed -n 's/.*install: done — *//p' "$INSTALL_LOG" | tail -1)"
if ! "$KEUR_BIN_DIR/keur" --version >/dev/null 2>&1; then
  case " $PRE_STATIC_RELEASES " in
    *" $installed "*)
      echo "  skip  published keur $installed is not self-contained (issue #36);" \
        "the wrapper contract is re-checked once a static release is out"
      summary; exit $? ;;
  esac
  fail "published keur ${installed:-?} does not start (keur --version failed)"
  summary; exit $?
fi
ok "published keur ${installed:-?} starts (keur --version)"

export PATH="$KEUR_BIN_DIR:$PATH"

# 2. keur is resolvable from PATH (the contract action.yml relies on for
#    subsequent workflow steps).
command -v keur >/dev/null 2>&1 \
  && ok "keur resolvable from PATH after install" \
  || fail "keur not on PATH after install"

# 3. Default Action invocation: `keur check` with nothing staged → PASS/exit 0.
REPO="$(make_temp_repo)"
(cd "$REPO" && keur check >/dev/null 2>&1) \
  && ok "keur check (nothing staged) → PASS (exit 0)" \
  || fail "keur check with empty stage should PASS"
rm -rf "$REPO"

# 4. args passthrough: `keur scan rules/` runs a different subcommand and never
#    blocks (exit 0) — proves the Action's `args` input is honoured, not ignored.
keur scan "$RULES" >/dev/null 2>&1 \
  && ok "keur scan rules/ (args passthrough) → exit 0" \
  || fail "keur scan should exit 0 (scan never blocks)"

summary
