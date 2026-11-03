#!/usr/bin/env bash
# Build from a clean source checkout; all generated output stays outside it.
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
lab=${1:?Usage: bash tools/linux/build.sh /absolute/external/build-directory}
lab=$(realpath -m -- "$lab")
case "$lab/" in "$source_root/"*) echo 'Build directory must be outside the source tree.' >&2; exit 2;; esac
jobs=${JOBS:-2}
case "$jobs" in 1|2) ;; *) echo 'JOBS must be 1 or 2.' >&2; exit 2;; esac
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
mkdir -p -- "$lab/engine" "$lab/tests" "$lab/logs" "$lab/tmp" "$lab/package"
export TMPDIR="$lab/tmp"

# Name the source revision that the agent interface reports in session.json.
# CI exports GITHUB_SHA, which src/CMakeLists.txt also uses as its default.
# A local build names its git commit, marked -dirty when tracked files differ
# from that commit, and "unknown" outside a git checkout.
if [[ -n ${GITHUB_SHA:-} ]]; then
    revision=$GITHUB_SHA
elif revision=$(git -C "$source_root" rev-parse --short=12 HEAD 2>/dev/null); then
    git -C "$source_root" diff --quiet HEAD -- 2>/dev/null || revision+=-dirty
else
    revision=unknown
fi

cmake -S "$source_root/src" -B "$lab/engine" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    -DOPENNEOUA_AGENT_FORK=six-factions -DOPENNEOUA_AGENT_REVISION="$revision" \
    2>&1 | tee "$lab/logs/configure-engine.log"
cmake --build "$lab/engine" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-engine.log"
cmake -S "$source_root/tests" -B "$lab/tests" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DOPENNEOUA_TRANSPORT_TEST=ON -DOPENNEOUA_AGENT_BRIDGE_TEST=ON -DOPENNEOUA_ENET_TEST=ON -DOPENNEOUA_ENET_ZND_TEST=ON \
    2>&1 | tee "$lab/logs/configure-tests.log"
cmake --build "$lab/tests" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-tests.log"
ctest --test-dir "$lab/tests" --output-on-failure 2>&1 | tee "$lab/logs/tests.log"
PYTHONDONTWRITEBYTECODE=1 python3 "$source_root/tests/test_six_faction_maps.py" -v \
    2>&1 | tee "$lab/logs/map-tests.log"
PYTHONDONTWRITEBYTECODE=1 python3 "$source_root/tests/test_agent_mcp.py" -v \
    2>&1 | tee "$lab/logs/agent-mcp-tests.log"
PYTHONDONTWRITEBYTECODE=1 python3 "$source_root/tests/test_faction_audio_manifest.py" -v \
    2>&1 | tee "$lab/logs/faction-audio-tests.log"
ldd "$lab/engine/OpenNeoUA" | tee "$lab/logs/runtime-libraries.log"
python3 - "$lab/logs/runtime-libraries.log" <<'PY'
from pathlib import Path
import sys
if 'not found' in Path(sys.argv[1]).read_text():
    raise SystemExit('An engine runtime library could not be resolved.')
PY
python3 "$source_root/tools/linux/package.py" --source "$source_root" \
    --binary "$lab/engine/OpenNeoUA" --output "$lab/package/OpenNeoUA-linux.tar.gz" \
    --epoch "$SOURCE_DATE_EPOCH"
