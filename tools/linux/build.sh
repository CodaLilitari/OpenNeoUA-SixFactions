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

cmake -S "$source_root/src" -B "$lab/engine" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    2>&1 | tee "$lab/logs/configure-engine.log"
cmake --build "$lab/engine" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-engine.log"
cmake -S "$source_root/tests" -B "$lab/tests" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DOPENNEOUA_TRANSPORT_TEST=ON \
    2>&1 | tee "$lab/logs/configure-tests.log"
cmake --build "$lab/tests" --parallel "$jobs" 2>&1 | tee "$lab/logs/build-tests.log"
ctest --test-dir "$lab/tests" --output-on-failure 2>&1 | tee "$lab/logs/tests.log"
PYTHONDONTWRITEBYTECODE=1 python3 "$source_root/tests/test_six_faction_maps.py" -v \
    2>&1 | tee "$lab/logs/map-tests.log"
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
