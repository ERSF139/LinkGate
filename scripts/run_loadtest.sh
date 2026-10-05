#!/usr/bin/env bash
# 用 1、2、4 个 IO 线程分别启动网关并压测一次，输出放在 build/loadtest/。
# 用法：scripts/run_loadtest.sh [设备数=10000] [每台频率Hz=1] [持续秒数=30]
set -euo pipefail

devices=${1:-10000}
rate=${2:-1}
duration=${3:-30}
root=$(cd "$(dirname "$0")/.." && pwd)
gateway="$root/build/release/gateway/linkgate"
loadgen="$root/build/release/tools/loadgen"
out="$root/build/loadtest"
mkdir -p "$out"

[[ -x "$gateway" && -x "$loadgen" ]] || {
  echo "build first: cmake --preset release && cmake --build --preset release -j" >&2
  exit 1
}
ulimit -n 65535 2>/dev/null || echo "warning: could not raise fd limit, current $(ulimit -n)" >&2

for threads in 1 2 4; do
  tag="d${devices}_r${rate}_io${threads}"
  echo "=== io-threads=$threads devices=$devices rate=${rate}Hz ==="
  "$gateway" --io-threads="$threads" --stats-interval-ms=5000 >"$out/$tag.gateway.log" 2>&1 &
  gw_pid=$!
  sleep 1
  "$loadgen" --devices="$devices" --rate="$rate" --subscribers=1 --threads=2 \
    --duration="$duration" --ramp=2000 | tee "$out/$tag.loadgen.txt"
  ps -o pid,pcpu,rss,cmd -p "$gw_pid" | tee -a "$out/$tag.loadgen.txt"
  kill -INT "$gw_pid"
  wait "$gw_pid" || true
  sleep 1
done
echo "results in $out"
