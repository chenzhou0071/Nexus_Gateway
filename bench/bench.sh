#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."

BENCH_ROOT="/tmp/nexus_bench"
LOG_DIR="$HOME/.local/share/nexus_bench_logs"
mkdir -p "$BENCH_ROOT/static" "$LOG_DIR"

pkill -9 -f nexus_gateway >/dev/null 2>&1 || true
pkill -9 upstream_server >/dev/null 2>&1 || true
pkill -9 wrk >/dev/null 2>&1 || true
sleep 1

if [ ! -f "$BENCH_ROOT/big.bin" ]; then
    dd if=/dev/urandom of="$BENCH_ROOT/big.bin" bs=1M count=50 status=none
fi
cp "$BENCH_ROOT/big.bin" "$BENCH_ROOT/static/"

cat > "$BENCH_ROOT/test.ini" <<'INIEOF'
[server]
listen       = 0.0.0.0:8080
worker_num   = auto
log_level    = info

[upstream.api]
server = 127.0.0.1:19995 weight=1

[upstream.static]
type = file
root = /tmp/nexus_bench/static

[route]
/api    = api
/static = static

[security]
rate_limit_per_ip = 10000
blacklist         =
INIEOF

gcc -o "$BENCH_ROOT/upstream_server" "$(dirname "$0")/upstream_server.c" -Wall -O2
"$BENCH_ROOT/upstream_server" >"$LOG_DIR/upstream.log" 2>&1 &
UP_PID=$!
sleep 0.5

./bin/nexus_gateway "$BENCH_ROOT/test.ini" > "$LOG_DIR/gateway.log" 2>&1 &
GW_PID=$!
sleep 1

cleanup() {
    kill $GW_PID 2>/dev/null || true
    kill $UP_PID 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Proxy 1KB ==="
wrk -t8 -c1000 -d10s http://127.0.0.1:8080/api 2>&1 | tee "$LOG_DIR/wrk_proxy.txt"

echo ""
echo "=== Static 50MB ==="
wrk -t8 -c200 -d10s http://127.0.0.1:8080/static/big.bin 2>&1 | tee "$LOG_DIR/wrk_static.txt"

echo ""
echo "=== Done. Logs: $LOG_DIR ==="
