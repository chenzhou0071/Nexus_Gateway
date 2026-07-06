#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/../.."

mkdir -p /tmp/nexus_upstream
echo "hello-from-upstream" > /tmp/nexus_upstream/index.html
(cd /tmp/nexus_upstream && python3 -m http.server 19998 >/dev/null 2>&1) &
UP_PID=$!
sleep 0.5

curl -sf http://127.0.0.1:19998/ >/dev/null
echo "test_proxy_basic: upstream reachable"

kill $UP_PID 2>/dev/null || true
wait $UP_PID 2>/dev/null || true
