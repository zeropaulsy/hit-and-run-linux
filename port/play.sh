#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Build (if needed) and start the game normally: no gdb, no timeout.
# usage: port/play.sh --data DIR [--mod MOD]... [--debug] [--32] [--no-build] [game args...]
#   --data DIR   your copy's PC game data dir (the one holding art/, scripts/, *.rcf); required
#   --mod MOD    load a Mod Launcher mod: a .lmlm package (read in place) or an extracted mod folder;
#                repeatable, earlier mods win (port/compat/shar_mods.h)
#   --debug      use the debug build (build-linux-debug, asserts + debug output; no boot movies)
#   --32         use the legacy 32-bit x86 build (build-linux32, or build-linux32-debug with --debug)
#                (the x86-64 build is the default; --64 is accepted and changes nothing)
#   --no-build   skip the ninja step
# env: SHAR_RUN   run directory (default run/), set up on first use: symlinks to the data,
#                 private copies of simpsons.ini and Save1-4 (the game writes them)
#      SHAR_BUILD build dir (overrides --debug)
# The console output goes to logs/last-session.log in the run dir too. If the game crashes (or exits non-zero, or is
# interrupted), the log is kept as crashes/crash-<date>-<time>.log with its backtrace resolved to source lines
# (crash report: port/compat/shar_crash.h).
# Other SHAR_* variables (SHAR_FPS_CAP, SHAR_VSYNC, SHAR_NO_AUDIO, ...) pass through to the game.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN=${SHAR_RUN:-$ROOT/run}

DATA=; DEBUG=0; X32=; BUILD_STEP=1; MODS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --data) [ $# -ge 2 ] || { echo "--data needs a directory" >&2; exit 1; }; DATA=$2; shift 2 ;;
        --data=*) DATA=${1#--data=}; shift ;;
        --mod) [ $# -ge 2 ] || { echo "--mod needs a folder or .lmlm" >&2; exit 1; }; MODS+=("$2"); shift 2 ;;
        --mod=*) MODS+=("${1#--mod=}"); shift ;;
        --debug) DEBUG=1; shift ;;
        --32) X32=32; shift ;;
        --64) shift ;;
        --no-build) BUILD_STEP=0; shift ;;
        *) break ;;
    esac
done
if [ $DEBUG = 1 ]; then
    BUILD=${SHAR_BUILD:-$ROOT/build-linux$X32-debug}; CMAKE_ARGS=(-DSHAR_RAD_DEBUG=ON)
else
    BUILD=${SHAR_BUILD:-$ROOT/build-linux$X32}; CMAKE_ARGS=()
fi
if [ -n "$X32" ]; then CMAKE_ARGS+=(-DSHAR_64BIT=OFF); else CMAKE_ARGS+=(-DSHAR_64BIT=ON); fi

if [ -z "$DATA" ]; then
    echo "usage: port/play.sh --data DIR [--mod MOD]... [--debug] [--32] [--no-build] [game args...]" >&2
    echo "  DIR is your copy's PC game data dir (holding art/, scripts/, *.rcf)" >&2; exit 1
fi
[ -d "$DATA" ] || { echo "Game data dir not found: $DATA" >&2; exit 1; }
DATA=$(cd "$DATA" && pwd)   # absolute, for the symlinks

# Run directory
mkdir -p "$RUN"
for f in ambience.rcf carsound.rcf dialog.rcf dialogf.rcf dialogg.rcf dialogs.rcf music00.rcf music01.rcf music02.rcf music03.rcf nis.rcf \
         scripts.rcf soundfx.rcf opening.bnr art movies scripts sound; do
    [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && ln -s "$DATA/$f" "$RUN/$f"
done
for f in simpsons.ini Save1 Save2 Save3 Save4; do
    [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && cp "$DATA/$f" "$RUN/$f"
done
# Shaders (post-processing, materials): the game dir's shaders/ if it has one, else a copy of the examples (yours to
# edit; examples added later are copied in without touching files already there)
if [ ! -e "$RUN/shaders" ]; then
    if [ -d "$DATA/shaders" ]; then ln -s "$DATA/shaders" "$RUN/shaders"; else cp -r "$ROOT/shaders" "$RUN/shaders"; fi
elif [ -d "$RUN/shaders" ] && [ ! -L "$RUN/shaders" ]; then
    cp -rn "$ROOT/shaders/." "$RUN/shaders/"
fi

# Mods: absolute paths in SHAR_MODS (the game runs from the run dir)
MOD_PATHS=
for m in "${MODS[@]}"; do
    [ -e "$m" ] || { echo "Mod not found: $m" >&2; exit 1; }
    m=$(realpath "$m")
    case "$m" in *:*) echo "Mod paths cannot contain ':': $m" >&2; exit 1 ;; esac
    MOD_PATHS=${MOD_PATHS:+$MOD_PATHS:}$m
done
[ -n "$MOD_PATHS" ] && export SHAR_MODS=$MOD_PATHS

# Build
if [ $BUILD_STEP = 1 ]; then
    # configured on first use; a build dir made for the other architecture (e.g. a 32-bit build-linux from before
    # x86-64 became the default) is switched over
    if [ ! -f "$BUILD/build.ninja" ] || { [ -z "$SHAR_BUILD" ] &&
            ! grep -q "^SHAR_64BIT:BOOL=$([ -n "$X32" ] && echo OFF || echo ON)\$" "$BUILD/CMakeCache.txt"; }; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja "${CMAKE_ARGS[@]}"
    fi
    echo "Building $BUILD ..."
    ninja -C "$BUILD" -j"$(nproc)" > "$ROOT/port/build.log" 2>&1 || {
        grep -E "error|FAILED" "$ROOT/port/build.log" | head -20; echo "Build failed, see port/build.log" >&2; exit 1; }
fi
[ -x "$BUILD/srr2" ] || { echo "$BUILD/srr2 not found" >&2; exit 1; }

cd "$RUN"
mkdir -p logs
LOG=$RUN/logs/last-session.log
{
    echo "SHAR session $(date '+%Y-%m-%d %H:%M:%S')"
    echo "build: $BUILD/srr2 ($(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo unknown))"
    echo "data: $DATA"
    echo "args: $*"
    [ -n "$MOD_PATHS" ] && echo "mods: $MOD_PATHS"
    env | grep -E '^SHAR_' | grep -v '^SHAR_MODS=' | sed 's/^/env: /' || true
    echo
} > "$LOG"
set +e
# tee -i: a Ctrl+C reaches the game but not tee, so the game's last output still lands in the log
"$BUILD/srr2" "$@" 2>&1 | tee -i -a "$LOG"
STATUS=${PIPESTATUS[0]}
set -e
[ $STATUS = 0 ] && exit 0

if [ $STATUS -gt 128 ]; then
    REASON="killed by SIG$(kill -l $((STATUS - 128)) 2>/dev/null || echo $((STATUS - 128)))"
else
    REASON="exit status $STATUS"
fi
echo "=== game ended abnormally: $REASON ===" >> "$LOG"
mkdir -p crashes
CRASH=$RUN/crashes/crash-$(date '+%Y%m%d-%H%M%S').log
python3 "$ROOT/port/crash_symbolize.py" "$BUILD/srr2" "$LOG" > "$CRASH" 2>/dev/null || cp "$LOG" "$CRASH"
echo "Game ended abnormally ($REASON). Log saved to $CRASH" >&2
exit $STATUS
