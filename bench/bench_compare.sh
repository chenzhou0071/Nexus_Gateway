#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."

# 编译两个版本：默认（有 sendfile） 和 NX_NO_SENDFILE=1（用 read/write）
echo "=== With sendfile ==="
make clean > /dev/null
make > /dev/null
./bin/nexus_gateway config.ini.example > /tmp/nexus_gw.log 2>&1 &
PID=$!
sleep 1
wrk -t8 -c200 -d10s http://127.0.0.1:8080/static/big.bin 2>&1 | tee /tmp/with_zc.txt
kill $PID; wait 2>/dev/null

echo ""
echo "=== Without sendfile (manual) ==="
echo "(需手工把 src/static_file.c 中 nexus_zc_sendfile 改为 read+write)"
echo "  1. 打开 src/static_file.c"
echo "  2. 把 nexus_zc_sendfile(client_fd, fd, &off, (size_t)remaining)"
echo "     替换为 read(fd, buf, ...) + write(client_fd, buf, n)"
echo "  3. 重新 make 并跑 wrk，结果存到 /tmp/without_zc.txt"
