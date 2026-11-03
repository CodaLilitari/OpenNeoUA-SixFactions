#!/usr/bin/env bash
# Start one OpenNeoUA instance on its own Xvfb display with the agent bridge enabled.
# Usage: launch_instance.sh NAME DISPLAY_NUM RUNTIME_DIR LAB_DIR [engine args...]
#   NAME         instance label (host, c1, bot1 ...); also the agent dir name
#   DISPLAY_NUM  X display number, e.g. 151 (one Xvfb per instance: no focus stealing,
#                which would cancel agent requests)
#   RUNTIME_DIR  per-instance copy of the assembled runtime (never shared: Env, Save and
#                the INI files are written)
#   LAB_DIR      absolute run directory OUTSIDE any git checkout (agent/, logs/, pids/)
#   engine args  e.g. -net-host 132 -net-name HOST1 -net-start-when 2, or
#                -bot Ghorkov:Veteran -net-join 127.0.0.1 -net-name BOT1 -net-ready
# Run it inside the match's network namespace: nsenter -t NSPID -n bash launch_instance.sh ...
# Env passthrough: OPENNEOUA_TRANSPORT=enet selects ENet (set it for EVERY peer or none).
#                  NICE (default 5), LP_NUM_THREADS (default 1), XVFB_SCREEN (default 1024x768x24)
set -euo pipefail
name=$1 disp=$2 rt=$(realpath -- "$3") lab=$(realpath -m -- "$4"); shift 4
agent="$lab/agent/$name"; logs="$lab/logs"; pids="$lab/pids"
mkdir -p -- "$agent" "$logs" "$pids"
chmod 700 -- "$agent"
# Xvfb: one server per instance, no TCP. Reuse the display on a relaunch (rejoin drill).
if [ -f "$pids/$name.xvfb.pid" ] && kill -0 "$(cat "$pids/$name.xvfb.pid")" 2>/dev/null; then
    [ -f "$pids/$name.display" ] && disp=$(cat "$pids/$name.display")
    echo "reusing Xvfb :$disp"
else
    Xvfb ":$disp" -screen 0 "${XVFB_SCREEN:-1024x768x24}" -nolisten tcp -noreset \
        </dev/null >"$logs/$name.xvfb.log" 2>&1 &
    echo $! >"$pids/$name.xvfb.pid"
    for _ in $(seq 50); do [ -S "/tmp/.X11-unix/X$disp" ] && break; sleep 0.1; done
    if ! kill -0 "$(cat "$pids/$name.xvfb.pid")" 2>/dev/null; then
        echo "Xvfb :$disp did not start (display in use?); see $logs/$name.xvfb.log" >&2
        exit 1
    fi
    echo "$disp" >"$pids/$name.display"
fi
# The engine truncates Data/Env/ypa_log.txt (log and netlog) at every start: keep the
# previous one, so a relaunched instance's pre-drop log survives.
if [ -f "$rt/Data/Env/ypa_log.txt" ]; then
    cp -- "$rt/Data/Env/ypa_log.txt" "$logs/$name.ypa_log.$(date +%Y%m%d-%H%M%S).txt"
fi
printf '%s\n' "$*" >>"$logs/$name.args"
# A previous engine's receipts are stale: waiters must see this engine's own session.json.
rm -f -- "$agent/session.json" "$agent/response.json" "$agent/frame.ppm"
cd -- "$rt"
env DISPLAY=":$disp" SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    LP_NUM_THREADS="${LP_NUM_THREADS:-1}" ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy \
    OPENNEOUA_AGENT_DIR="$agent" ${OPENNEOUA_TRANSPORT:+OPENNEOUA_TRANSPORT=$OPENNEOUA_TRANSPORT} \
    nice -n "${NICE:-5}" ./OpenNeoUA "$@" </dev/null >>"$logs/$name.stdout.log" 2>>"$logs/$name.stderr.log" &
echo $! >"$pids/$name.pid"
echo "started $name pid=$(cat "$pids/$name.pid") display=:$disp agent=$agent"
