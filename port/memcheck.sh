#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Run the game under valgrind memcheck to find reads of uninitialised memory, then summarise the reports by
# where the memory came from (which allocation / stack frame), most frequent first.
# usage: port/memcheck.sh [seconds] [game args...]          e.g. port/memcheck.sh 330 skipfe
# env:   SHAR_RUN    run directory (default run/; use a scratch one with display=window, resolution=640x480)
#        SHAR_BUILD  build dir (default build-linux, x86-64: -O0 -g, which keeps memcheck's line numbers exact;
#                    build-linux32 for the legacy 32-bit build)
#        VG_TOOLS    where valgrind and its helpers are unpacked (default ~/.cache/shar-memcheck)
#        VG_OPTS     extra valgrind options
# Output: $RUN/memcheck.log (raw), $RUN/memcheck.txt (summary). Other SHAR_* variables pass through to the game,
# e.g. SHAR_KEY_SCRIPT to move around (SDL_GetTicks times; under memcheck Level 1 is playable after ~3 minutes),
# SHAR_MAX_FRAME_MS=33 for driving (slow motion, so the world streams in ahead of the car).
#
# Every allocation from the game's heaps and FBMemoryPool is marked undefined (memorymanager.cpp, memorypool.cpp),
# so recycled memory counts as uninitialised too. Known noise: SDL/X11/udev/dbus during window creation, and
# glibc's vector string routines reading past the terminator of strcpy'd buffers (printf/sscanf/snprintf of
# stack buffers, LoadingManager request names).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN=${SHAR_RUN:-$ROOT/run}
BUILD=${SHAR_BUILD:-$ROOT/build-linux}
T=${VG_TOOLS:-$HOME/.cache/shar-memcheck}
SECS=${1:-330}; shift || true

# One-time setup, no root needed: valgrind (ships the x86 and amd64 tools), and glibc's debug symbols merged into a
# private copy of the ELF loader (valgrind needs ld.so's strlen symbol; the system one is stripped). The game is run
# as a copy whose ELF interpreter points at that loader. The loader and debug package follow the build's ELF class
# (32-bit: ld-linux.so.2 + libc6-dbg:i386; 64-bit: ld-linux-x86-64.so.2 + libc6-dbg, from the system if installed).
if [ "$(readelf -h "$BUILD/srr2" | awk '/Class:/{print $2}')" = ELF64 ]; then
    L=/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2; DBG=libc6-dbg:amd64
else
    L=/usr/lib/i386-linux-gnu/ld-linux.so.2; DBG=libc6-dbg:i386
fi
LDCOPY="$T/$(basename "$L")"
if [ ! -x "$T/vg/usr/bin/valgrind" ] || [ ! -f "$LDCOPY" ]; then
    mkdir -p "$T/deb" && cd "$T/deb"
    [ -x "$T/vg/usr/bin/valgrind" ] || apt-get download valgrind elfutils patchelf libelf1t64 libdw1t64 > /dev/null
    for d in valgrind_*.deb; do dpkg-deb -x "$d" "$T/vg"; done
    for d in elfutils_*.deb patchelf_*.deb libelf*.deb libdw*.deb; do dpkg-deb -x "$d" "$T/tools"; done
    ID=$(readelf -n "$L" | awk '/Build ID/{print $3}')
    SYM="/usr/lib/debug/.build-id/${ID:0:2}/${ID:2}.debug"
    if [ ! -f "$SYM" ]; then
        rm -f libc6-dbg_*"${DBG#*:}".deb; apt-get download "$DBG" > /dev/null
        for d in libc6-dbg_*"${DBG#*:}".deb; do dpkg-deb -x "$d" "$T/tools"; done
        SYM="$T/tools$SYM"
    fi
    LD_LIBRARY_PATH="$T/tools/usr/lib/x86_64-linux-gnu" "$T/tools/usr/bin/eu-unstrip" -o "$LDCOPY" "$L" "$SYM"
fi

ninja -C "$BUILD" -j"$(nproc)" > "$ROOT/port/build.log" 2>&1 || { grep -E "error|FAILED" "$ROOT/port/build.log" | head -20; exit 1; }
# one copy per run, so several runs (separate SHAR_RUN dirs) can go at once
GAME="$T/srr2.$$"
cp "$BUILD/srr2" "$GAME"
LD_LIBRARY_PATH="$T/tools/usr/lib/x86_64-linux-gnu" "$T/tools/usr/bin/patchelf" --set-interpreter "$LDCOPY" "$GAME"

cd "$RUN"
set +e
# nouserintercepts: the game replaces operator new/delete itself; memcheck must not swap in its own.
# fair-sched: valgrind runs one thread at a time; without it the main and load threads handing the load mutex
# back and forth starved the drive thread, and Level 1 took over 15 minutes to load instead of ~3.
# X11 (XWayland): on Wayland a libglycin thread (window decorations) segfaults under valgrind at window creation.
VALGRIND_LIB="$T/vg/usr/libexec/valgrind" SHAR_X11=${SHAR_X11:-1} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy} \
timeout -s INT "$SECS" "$T/vg/usr/bin/valgrind" --tool=memcheck --error-limit=no --num-callers=24 --track-origins=yes --fair-sched=yes \
    --leak-check=no --soname-synonyms=somalloc=nouserintercepts $VG_OPTS --log-file="$RUN/memcheck.log" "$GAME" "$@" \
    > "$RUN/memcheck.out" 2>&1
rm -f "$GAME"
python3 "$ROOT/port/memcheck_summary.py" "$RUN/memcheck.log" > "$RUN/memcheck.txt"
grep "^###" "$RUN/memcheck.txt" | cut -c1-200
echo "summary: $RUN/memcheck.txt   raw: $RUN/memcheck.log"
