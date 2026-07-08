#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${CERIOUS_ENV_FILE:-$ROOT/.env}"
LOG_DIR="${CERIOUS_LOG_DIR:-$ROOT/data/logs}"
mkdir -p "$LOG_DIR"

if [ -f "$ENV_FILE" ]; then
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

BACKEND_HOST="${CERIOUS_BACKEND_HOST:-127.0.0.1}"
BACKEND_PORT="${CERIOUS_BACKEND_PORT:-8000}"
EXCHANGE_HOST="${CERIOUS_EXCHANGE_HOST:-127.0.0.1}"
EXCHANGE_PORT="${CERIOUS_EXCHANGE_HTTP_PORT:-8011}"

EXCHANGE_BIN="${CERIOUS_EXCHANGE_BIN:-$ROOT/native/cerious-exchange-cpp/build/cerious_exchange_server}"
GATEWAY_BIN="${CERIOUS_GATEWAY_BIN:-$ROOT/native/gateway-cpp/build/cerious_gateway}"

if [ ! -x "$EXCHANGE_BIN" ]; then
  echo "Exchange binary not found: $EXCHANGE_BIN" >&2
  echo "Run npm run build:native:unix first." >&2
  exit 1
fi

if [ ! -x "$GATEWAY_BIN" ]; then
  echo "Gateway binary not found: $GATEWAY_BIN" >&2
  echo "Run npm run build:native:unix first." >&2
  exit 1
fi

PIDS=()
cleanup() {
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
}
trap cleanup EXIT INT TERM

echo "Starting Cerious Exchange on $EXCHANGE_HOST:$EXCHANGE_PORT"
"$EXCHANGE_BIN" --host "$EXCHANGE_HOST" --port "$EXCHANGE_PORT" --root "$ROOT" >"$LOG_DIR/cerious-exchange.log" 2>&1 &
PIDS+=("$!")

sleep 0.5

echo "Starting Cerious Gateway on $BACKEND_HOST:$BACKEND_PORT"
"$GATEWAY_BIN" \
  --host "$BACKEND_HOST" \
  --port "$BACKEND_PORT" \
  --execution-host "$EXCHANGE_HOST" \
  --execution-port "$EXCHANGE_PORT" \
  --root "$ROOT" >"$LOG_DIR/cerious-gateway.log" 2>&1 &
PIDS+=("$!")

echo "Cerious backend started."
echo "Gateway:  http://$BACKEND_HOST:$BACKEND_PORT/"
echo "Health:   http://$BACKEND_HOST:$BACKEND_PORT/api/health"
echo "Logs:     $LOG_DIR"
echo
echo "Press Ctrl+C to stop this local backend."

wait
