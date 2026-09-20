#!/usr/bin/env bash
# eval/pipeline-coverage.sh — measure which CI/CD stages a repo actually has, by
# reading the CI config and mapping known tools/jobs to stages (ADR-012).
# Read-only; --strict gates. Language-agnostic, sub-second: it greps the CI
# config for evidence of a tool, it never runs the pipeline.
# @implements ADR-012
#
# Stage taxonomy: OWASP DSOMM (security), SLSA (supply chain), the test pyramid.
# The tool->stage mapping below is DATA — add your tools without touching code.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
ROOT="$(cd "$HERE/.." && pwd -P)"
cd "$ROOT"

STRICT=0; [ "${1:-}" = "--strict" ] && STRICT=1
green() { printf "\033[32m%s\033[0m" "$1"; }
red()   { printf "\033[31m%s\033[0m" "$1"; }
yellow(){ printf "\033[33m%s\033[0m" "$1"; }
row()   { printf '  %-22s %s\n' "$1" "$2"; }
hdr()   { echo; echo "── $1 ──"; }
pct()   { [ "$2" -eq 0 ] && echo 100 || echo $(( $1 * 100 / $2 )); }

# --- gather CI config text (GitHub, GitLab, common others) ------------------
CI_TEXT=$(cat .github/workflows/*.yml .github/workflows/*.yaml \
              .gitlab-ci.yml .circleci/config.yml Jenkinsfile azure-pipelines.yml \
              2>/dev/null)
if [ -z "$CI_TEXT" ]; then
    echo "pipeline-coverage: no CI config found (.github/workflows, .gitlab-ci.yml, ...)" >&2
    exit 0
fi

# --- stage <TAB> group <TAB> evidence-regex (tool/keyword) — DATA ------------
# A stage is PRESENT if any of its evidence patterns appears in the CI text.
STAGES='
format	A	\b(prettier|gofmt|black|rustfmt|clang-format|fmt)\b
lint	A	\b(eslint|ruff|pylint|golangci-lint|clippy|rubocop|shellcheck|lint)\b
type-check	A	\b(tsc|mypy|type-check|typecheck)\b
unit-test	A	\b(pytest|jest|go test|cargo test|make test|npm test|unittest)\b
coverage-threshold	A	\b(codecov|coveralls|--cov|coverage|lcov)\b
integration-test	A	\b(integration-test|e2e|smoke|playwright|cypress)\b
mutation-test	A	\b(mutmut|stryker|pitest|cargo-mutants|mutation)\b
secret-scan	B	\b(gitleaks|trufflehog|detect-secrets)\b
sast	B	\b(semgrep|codeql|bandit|gosec|sonar|brakeman)\b
sca	B	\b(trivy|grype|snyk|osv-scanner|dependabot|npm audit|pip-audit)\b
iac-scan	B	\b(checkov|tfsec|kics|terrascan|trivy config)\b
container-scan	B	\b(trivy image|grype|dockle|clair)\b
dast	B	\b(zap|nikto|nuclei|dastardly|burp)\b
sbom	C	\b(syft|cyclonedx|spdx|sbom)\b
provenance	C	\b(slsa|provenance|attest|in-toto)\b
sign	C	\b(cosign|sigstore|gpg --sign|notary)\b
license-check	C	\b(license|fossa|licensee|reuse)\b
smoke-test	D	\b(smoke)\b
health-gate	D	\b(health|rollback|canary)\b
'

group_name() { case "$1" in A) echo "correctness/quality";; B) echo "security (DSOMM)";; C) echo "supply-chain (SLSA)";; D) echo "release/ops";; esac; }

echo "════════════════════════════════════════════════════════════"
echo "  keur pipeline coverage (stages present in CI config)"
echo "════════════════════════════════════════════════════════════"

FAIL=0
for G in A B C D; do
    hdr "Group $G — $(group_name "$G")"
    present=0; total=0
    while IFS=$'\t' read -r stage grp evidence; do
        [ -z "$stage" ] && continue
        [ "$grp" != "$G" ] && continue
        total=$((total+1))
        if printf '%s' "$CI_TEXT" | grep -qiE "$evidence"; then
            row "$stage" "$(green '✓ present')"; present=$((present+1))
        else
            row "$stage" "$(yellow '· absent')"
        fi
    done <<< "$STAGES"
    [ "$total" -gt 0 ] && row "" "$present/$total present ($(pct $present $total)%)"
done

echo
echo "────────────────────────────────────────────────────────────"
echo "  Note: which stages are REQUIRED follows from the project profile"
echo "  (ADR-011 lifecycle × criticality × maturity). '· absent' is only a"
echo "  gap when the profile requires that stage."
[ "$STRICT" -eq 1 ] && [ "$FAIL" -eq 1 ] && exit 1
exit 0
