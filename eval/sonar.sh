#!/usr/bin/env bash
# eval/sonar.sh — manage a local, Dockerised SonarQube for ground-truth runs.
#
# SonarQube is a server (not a CLI), so it runs as a container and findings are
# read back over its web API by the `sonar` adapter in groundtruth.sh. This
# helper makes the setup reproducible: start the server, mint an analysis token
# into a gitignored env file, check status, and tear down.
#
# Usage:
#   eval/sonar.sh up        start the server (idempotent) + wait until UP
#   eval/sonar.sh token     (re)generate an analysis token -> eval/.run/sonar.env
#   eval/sonar.sh status    show server status + token presence
#   eval/sonar.sh down      stop + remove the server container (keeps volumes)
#   eval/sonar.sh purge     down + remove the data volumes (full reset)
#
# Env (override if needed):
#   SONAR_HOST   default http://localhost:9000
#   SONAR_ADMIN_PW  admin password to set/use (default: Keur-eval-3-local)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
ENV_FILE="$HERE/.run/sonar.env"
NAME="keur-sonar"
IMAGE="sonarqube:lts-community"
HOST="${SONAR_HOST:-http://localhost:9000}"
ADMIN_PW="${SONAR_ADMIN_PW:-Keur-eval-3-local}"

have() { command -v "$1" >/dev/null 2>&1; }
have docker || { echo "sonar.sh: docker not found / not running" >&2; exit 1; }

wait_up() {
  echo "sonar.sh: waiting for SonarQube to be UP (can take a few minutes)..."
  for _ in $(seq 1 60); do
    if curl -fs "$HOST/api/system/status" 2>/dev/null | grep -q '"status":"UP"'; then
      echo "sonar.sh: UP"; return 0
    fi
    sleep 5
  done
  echo "sonar.sh: timed out waiting for UP" >&2; return 1
}

mint_token() {
  # First-login password change is required once; ignore if already changed.
  curl -s -u admin:admin -X POST "$HOST/api/users/change_password" \
    --data-urlencode "login=admin" \
    --data-urlencode "previousPassword=admin" \
    --data-urlencode "password=$ADMIN_PW" >/dev/null 2>&1
  # Revoke a same-named token first so this is idempotent.
  curl -s -u "admin:$ADMIN_PW" -X POST "$HOST/api/user_tokens/revoke" \
    --data-urlencode "name=keur-eval-3" >/dev/null 2>&1
  local tok
  tok="$(curl -s -u "admin:$ADMIN_PW" -X POST "$HOST/api/user_tokens/generate" \
         --data-urlencode "name=keur-eval-3" \
         | python3 -c 'import sys,json;print(json.load(sys.stdin).get("token",""))' 2>/dev/null)"
  [ -n "$tok" ] || { echo "sonar.sh: token generation failed" >&2; return 1; }
  mkdir -p "$HERE/.run"
  cat > "$ENV_FILE" <<EOF
# Local SonarQube credentials for the ground-truth run. gitignored — do NOT
# commit. Regenerate with: eval/sonar.sh token
export SONAR_HOST=$HOST
export SONAR_TOKEN=$tok
EOF
  echo "sonar.sh: token written to $ENV_FILE"
}

case "${1:-status}" in
  up)
    if docker ps -a --format '{{.Names}}' | grep -qx "$NAME"; then
      docker start "$NAME" >/dev/null 2>&1 || true
    else
      docker run -d --name "$NAME" -p 9000:9000 \
        -e SONAR_ES_BOOTSTRAP_CHECKS_DISABLE=true \
        -v keur-sonar-data:/opt/sonarqube/data \
        -v keur-sonar-logs:/opt/sonarqube/logs \
        -v keur-sonar-exts:/opt/sonarqube/extensions \
        "$IMAGE" >/dev/null
    fi
    wait_up && mint_token
    ;;
  token)  mint_token ;;
  status)
    docker ps --filter "name=$NAME" --format '{{.Names}}\t{{.Status}}' || true
    curl -fs "$HOST/api/system/status" 2>/dev/null || echo "(server not reachable)"
    echo
    [ -f "$ENV_FILE" ] && echo "token: present ($ENV_FILE)" || echo "token: none"
    ;;
  down)   docker rm -f "$NAME" >/dev/null 2>&1 && echo "sonar.sh: removed $NAME (volumes kept)" ;;
  purge)
    docker rm -f "$NAME" >/dev/null 2>&1 || true
    docker volume rm keur-sonar-data keur-sonar-logs keur-sonar-exts >/dev/null 2>&1 || true
    rm -f "$ENV_FILE"
    echo "sonar.sh: purged container + volumes + token"
    ;;
  *) echo "usage: sonar.sh up|token|status|down|purge" >&2; exit 1 ;;
esac
