#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Rebuild, then run the game from the run directory under gdb; on a crash print the backtrace.
# usage: port/run.sh [seconds] [game args...]     (run directory: $SHAR_RUN, default run/)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN=${SHAR_RUN:-$ROOT/run}
SECS=${1:-60}; shift || true
BUILD=${SHAR_BUILD:-$ROOT/build-linux}
ninja -C "$BUILD" -j6 > "$ROOT/port/build.log" 2>&1 || { grep -E "error|FAILED" "$ROOT/port/build.log" | head -20; exit 1; }
cd "$RUN"
set +e
timeout "$SECS" gdb -q -batch -ex "set debuginfod enabled off" -ex "handle SIGPIPE nostop" -ex run -ex bt \
    --args "$BUILD/srr2" "$@" > "$RUN/run.log" 2>&1
grep -vE "^\[New Thread|^\[Thread .* exited|libthread_db|Using host|^\[Inferior" "$RUN/run.log" | tail -${TAIL:-45}
