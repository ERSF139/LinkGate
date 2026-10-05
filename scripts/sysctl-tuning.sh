#!/usr/bin/env bash
# 压测上万条连接前需要调整的内核参数。不带参数时只打印当前值；带 --apply 时写入（需要 root），重启后失效。
# 只会调大，不会把已经更大的值改小。
set -euo pipefail

numeric_keys=(net.core.somaxconn net.ipv4.tcp_max_syn_backlog fs.file-max fs.nr_open)
declare -A wanted=(
  [net.core.somaxconn]=65535
  [net.ipv4.tcp_max_syn_backlog]=65535
  [fs.file-max]=1048576
  [fs.nr_open]=1048576
)
port_range_key=net.ipv4.ip_local_port_range
port_range_low=1024
port_range_high=65000

apply=false
[[ "${1:-}" == "--apply" ]] && apply=true

for key in "${numeric_keys[@]}"; do
  current=$(sysctl -n "$key")
  action="ok"
  if (( current < ${wanted[$key]} )); then
    action="raise"
    $apply && sysctl -q -w "$key=${wanted[$key]}"
  fi
  printf '%-32s current=%-20s wanted>=%-10s %s\n' "$key" "$current" "${wanted[$key]}" "$action"
done

read -r low high < <(sysctl -n "$port_range_key")
action="ok"
if (( low > port_range_low || high < port_range_high )); then
  action="widen"
  $apply && sysctl -q -w "$port_range_key=$port_range_low $port_range_high"
fi
printf '%-32s current=%-20s wanted=%-11s %s\n' "$port_range_key" "$low $high" "$port_range_low $port_range_high" "$action"

echo
echo "per-process fd limit (ulimit -n): $(ulimit -n), hard: $(ulimit -Hn)"
echo "run 'ulimit -n 65535' in every shell that starts linkgate or loadgen"
$apply || echo "dry run; re-run with --apply (as root) to change the values"
