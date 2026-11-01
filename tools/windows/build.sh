#!/usr/bin/env bash
# Run from the official MSYS2 MINGW64 shell; generated files stay external.
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
lab=${1:?Usage: bash tools/windows/build.sh /absolute/external/build-directory}
lab=$(realpath -m -- "$lab")
case "$lab/" in "$source_root/"*) echo 'Build output must be outside the source tree.' >&2; exit 2;; esac
[[ ${MSYSTEM:-} == MINGW64 ]] || { echo 'Use the MSYS2 MINGW64 shell.' >&2; exit 2; }
jobs=${JOBS:-2}
case "$jobs" in 1|2) ;; *) echo 'JOBS must be 1 or 2.' >&2; exit 2;; esac
mkdir -p -- "$lab/engine" "$lab/tests" "$lab/logs" "$lab/tmp" "$lab/package"
export TMPDIR="$lab/tmp"
export TEMP=$(cygpath -w "$lab/tmp")
export TMP="$TEMP"
cmake -S "$source_root/src" -B "$lab/engine" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
  -DOPENNEOUA_AGENT_FORK=six-factions \
  2>&1 | tee "$lab/logs/configure-engine.log"
cmake --build "$lab/engine" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-engine.log"
cmake -S "$source_root/tests" -B "$lab/tests" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENNEOUA_TRANSPORT_TEST=ON -DOPENNEOUA_AGENT_BRIDGE_TEST=ON \
  2>&1 | tee "$lab/logs/configure-tests.log"
cmake --build "$lab/tests" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-tests.log"
ctest --test-dir "$lab/tests" --output-on-failure 2>&1 | tee "$lab/logs/tests.log"
python -B "$source_root/tests/test_six_faction_maps.py" -v 2>&1 | tee "$lab/logs/map-tests.log"
python -B "$source_root/tests/test_agent_mcp.py" -v 2>&1 | tee "$lab/logs/agent-mcp-tests.log"
python -B "$source_root/tests/test_windows_package.py" -v 2>&1 | tee "$lab/logs/package-tests.log"
python -B "$source_root/tools/windows/package.py" --source "$source_root" \
  --binary "$lab/engine/OpenNeoUA.exe" --prefix "$MINGW_PREFIX" \
  --output "$lab/package/OpenNeoUA-windows-x86_64.zip" \
  2>&1 | tee "$lab/logs/package.log"
