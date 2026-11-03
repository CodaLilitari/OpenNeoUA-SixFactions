#!/usr/bin/env bash
# Sample CPU% / RSS / peak RSS / threads of every instance and Xvfb every N seconds into LAB/logs/resources.csv.
# Usage: sample.sh LAB_DIR [INTERVAL_S]   (stop with kill; cheap: reads /proc only)
set -u
lab=$(realpath -- "$1"); every=${2:-10}; out="$lab/logs/resources.csv"
[ -f "$out" ] || echo "epoch,name,pid,pcpu,rss_kb,hwm_kb,threads" >"$out"
while sleep "$every"; do
  for f in "$lab"/pids/*.pid; do
    n=$(basename "$f" .pid); p=$(cat "$f"); [ -d "/proc/$p" ] || continue
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$(date +%s)" "$n" "$p" "$(ps -o pcpu= -p "$p" | tr -d ' ')" \
      "$(awk '/VmRSS/{print $2}' /proc/$p/status)" "$(awk '/VmHWM/{print $2}' /proc/$p/status)" \
      "$(awk '/Threads/{print $2}' /proc/$p/status)" >>"$out"
  done
done
