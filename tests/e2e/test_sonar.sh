#!/usr/bin/env bash
# test_sonar.sh — keur guards sonar-project.properties configuration.
#
# SonarQube/SonarCloud silently tolerate a malformed properties file: unknown
# keys are ignored, missing ones fall back to defaults, and coverage simply
# reads 0% when no language report path is set. Nothing else validates this
# file. These SONAR-* rules give an early, local signal. This test proves the
# rules fire on real misconfigurations AND stay silent on correct configs
# (no false positives), including the per-language `requires:` gating.
#
# @see rules/quality/SONAR-*.rule, rules/security/SONAR-003-token-in-config.rule
source "$(dirname "$0")/helpers.sh"
set +o errexit
BINARY="$(resolve_binary "${1:-}")"
[[ -n "$BINARY" ]] || { echo "FAIL: keur binary not found"; exit 1; }

ROOT="$(repo_root)"
export KEUR_RULES_DIRS="$ROOT/rules"

echo "==> test_sonar ($BINARY)"

FX="$(mktemp -d "${TMPDIR:-/tmp}/keur_sonar.XXXXXX")"
trap 'rm -rf "$FX"' EXIT

# ids fired by a scan of a fixture dir, deduped (space-separated).
fired() { "$BINARY" scan "$1" 2>/dev/null | grep -oE 'SONAR-00[0-9]' | sort -u | tr '\n' ' '; }
has()  { case " $1 " in *" $2 "*) return 0;; *) return 1;; esac; }

# --- missing projectKey (SONAR-001) ----------------------------------------
d="$FX/nokey"; mkdir -p "$d"; printf 'const x=1\n' > "$d/a.ts"
printf 'sonar.organization=org\nsonar.sources=.\nsonar.javascript.lcov.reportPaths=coverage/lcov.info\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-001 && ok "SONAR-001 fires when sonar.projectKey is missing" \
                   || fail "SONAR-001 did not fire on missing projectKey" "got: $f"

# present projectKey -> SONAR-001 silent (no false positive)
d="$FX/haskey"; mkdir -p "$d"; printf 'const x=1\n' > "$d/a.ts"
printf 'sonar.projectKey=org_x\nsonar.organization=org\nsonar.sources=.\nsonar.javascript.lcov.reportPaths=coverage/lcov.info\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-001 && fail "SONAR-001 false-positive: projectKey IS present" "got: $f" \
                   || ok "SONAR-001 silent when projectKey present (no FP)"

# --- misspelled key (SONAR-004) --------------------------------------------
d="$FX/typo"; mkdir -p "$d"; printf 'const x=1\n' > "$d/a.ts"
printf 'sonar.projectKey=org_x\nsonar.exclusion=**/foo\nsonar.sources=.\nsonar.javascript.lcov.reportPaths=coverage/lcov.info\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-004 && ok "SONAR-004 fires on sonar.exclusion (should be plural)" \
                   || fail "SONAR-004 did not catch the misspelled key" "got: $f"

# --- token in config (SONAR-003) -------------------------------------------
d="$FX/tok"; mkdir -p "$d"; printf 'const x=1\n' > "$d/a.ts"
printf 'sonar.projectKey=org_x\nsonar.sources=.\nsonar.token=EXAMPLE_NOT_A_REAL_TOKEN\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-003 && ok "SONAR-003 fires on a committed sonar.token" \
                   || fail "SONAR-003 did not catch the committed token" "got: $f"

# token passed via a CI variable -> SONAR-003 silent (no false positive)
d="$FX/tokvar"; mkdir -p "$d"; printf 'const x=1\n' > "$d/a.ts"
printf 'sonar.projectKey=org_x\nsonar.sources=.\nsonar.token=${env.SONAR_TOKEN}\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-003 && fail "SONAR-003 false-positive on a \$-variable token" "got: $f" \
                   || ok "SONAR-003 silent when token is a CI variable (no FP)"

# --- per-language coverage: Java needs JaCoCo (SONAR-005) ------------------
d="$FX/java_bad"; mkdir -p "$d"; printf 'public class A {}\n' > "$d/A.java"
printf 'sonar.projectKey=org_x\nsonar.sources=src\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-005 && ok "SONAR-005 fires: Java repo, no JaCoCo coverage path" \
                   || fail "SONAR-005 did not fire on a Java repo without jacoco" "got: $f"

# Java repo WITH jacoco -> SONAR-005 silent (no false positive)
d="$FX/java_good"; mkdir -p "$d"; printf 'public class A {}\n' > "$d/A.java"
printf 'sonar.projectKey=org_x\nsonar.sources=src\nsonar.coverage.jacoco.xmlReportPaths=target/site/jacoco/jacoco.xml\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
has "$f" SONAR-005 && fail "SONAR-005 false-positive: jacoco path IS set" "got: $f" \
                   || ok "SONAR-005 silent when JaCoCo path present (no FP)"

# --- requires: gating — a JS repo must NOT trigger the Java rule -----------
d="$FX/js_only"; mkdir -p "$d"; printf 'console.log(1)\n' > "$d/a.js"; printf '{"name":"x"}\n' > "$d/package.json"
printf 'sonar.projectKey=org_x\nsonar.sources=.\n' > "$d/sonar-project.properties"
f="$(fired "$d")"
if has "$f" SONAR-006 && ! has "$f" SONAR-005; then
  ok "requires: gating — JS repo fires SONAR-006 (lcov) not SONAR-005 (jacoco)"
else
  fail "requires: gating wrong for a JS-only repo" "got: $f"
fi

summary
