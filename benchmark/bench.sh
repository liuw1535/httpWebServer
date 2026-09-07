#!/usr/bin/env bash
#
# CppExpress 压测脚本（依赖 wrk）
# 用法: ./benchmark/bench.sh [host] [port]
#
set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-3000}"
BASE="http://${HOST}:${PORT}"

if ! command -v wrk >/dev/null 2>&1; then
    echo "error: wrk not found. install with 'sudo apt install wrk' or 'brew install wrk'" >&2
    exit 1
fi

run() {
    local name="$1"; shift
    echo "==================== $name ===================="
    wrk "$@" --latency
    echo
}

run "Hello (4t/1000c/30s, Keep-Alive)"  -t4  -c1000 -d30s "${BASE}/"
run "Hello (8t/5000c/30s, Keep-Alive)"  -t8  -c5000 -d30s "${BASE}/api/hello"
run "Short conn (4t/1000c/30s)"         -t4  -c1000 -d30s -H "Connection: close" "${BASE}/"
run "Path param :id (4t/1000c/30s)"     -t4  -c1000 -d30s "${BASE}/api/users/42"
run "POST echo (4t/1000c/30s)"          -t4  -c1000 -d30s -s benchmark/post.lua "${BASE}/api/echo"
