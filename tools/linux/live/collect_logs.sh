#!/usr/bin/env bash
# Copy every instance's evidence into LAB/collected-<stamp>/: engine logs (ypa_log.txt with
# the netlog and the netauto lines, archived logs of earlier launches), INI files, hashes,
# the latest agent receipts, Xvfb/stdout/stderr logs and resources.csv, plus net-summary.txt.
# Usage: collect_logs.sh LAB_DIR RUNTIME_DIR...
set -euo pipefail
lab=$(realpath -- "$1"); shift
out="$lab/collected-$(date +%Y%m%d-%H%M%S)"; mkdir -p -- "$out"
for rt in "$@"; do
    n=$(basename -- "$rt"); mkdir -p -- "$out/$n"
    cp -a -- "$rt/Data/Env/." "$out/$n/Env/" 2>/dev/null || true
    cp -a -- "$rt/Data/NUCLEUS.INI" "$rt/Data/OpenNeoUA.ini" "$out/$n/" 2>/dev/null || true
    sha256sum "$rt/OpenNeoUA" "$rt"/Data/Levels/MULTI/l*.ldf > "$out/$n/hashes.txt" 2>/dev/null || true
done
cp -a -- "$lab/logs" "$lab/pids" "$out/" 2>/dev/null || true
for a in "$lab"/agent/*; do
    [ -d "$a" ] || continue
    mkdir -p "$out/agent/$(basename "$a")"
    cp -a "$a"/session.json "$a"/response.json "$out/agent/$(basename "$a")/" 2>/dev/null || true
done
# net-summary.txt: connection, roster and bot lines, each STARTBUILD sent, applied or refused,
# and the lines of network messages dropped before their handler (rejected as malformed,
# ignored in this phase of the game, or lost with the rest of a broken packet), which leave
# no line of their own.
summary='netauto:|Connection to|Connected to|READY TO PLAY|Waiting for player|destroy player|kicked off|RESTORE|UPD:|plays |is HOST|is CLIENT|bot:|STARTBUILD'
dropped='Rejected malformed|rejected message was|Ignored message|Strange Number of Messages|invalid message offset|truncated network message|has strange size'
grep -H -E "$summary|$dropped" "$out"/*/Env/ypa_log.txt > "$out/net-summary.txt" 2>/dev/null || true
echo "$out"
