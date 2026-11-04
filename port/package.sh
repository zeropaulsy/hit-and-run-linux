#!/bin/bash
# Package the release build for another machine: dist/shar-linux-<arch>-<date>/ and a .tar.gz of it, holding the
# game, the libraries it needs (except the ones that must come from the target system: glibc, the OpenGL / X11 /
# Wayland stack, which goes with the graphics driver), the example shaders and a launcher (run.sh).
# usage: port/package.sh [--32] [--noble]   (builds build-linux, x86-64, first; --32: the legacy 32-bit x86 build,
# build-linux32; --64 is accepted and changes nothing)
# --noble: build and package inside an Ubuntu 24.04 container (podman, port/noble.Containerfile; build dirs
# build-linux[32]-noble), so the game and its bundled libraries need only 24.04's glibc and the package runs on
# Ubuntu 24.04 / Linux Mint 22 and newer. Without it the package needs this machine's glibc.
# --appimage: also make dist/shar-linux-x86_64-<date>.AppImage of the same bundle (x86-64 only; needs mksquashfs and,
# the first time, curl for the runtime). Settings and saves go in ~/.local/share/simpsons-hit-and-run, or next to the
# AppImage when it sits in the game data folder.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SUFFIX=; NOBLE=; APPIMAGE=
for a in "$@"; do
    case "$a" in
        --32) SUFFIX=32 ;;
        --64) ;;
        --noble) NOBLE=1 ;;
        --appimage) APPIMAGE=1 ;;
        *) echo "usage: port/package.sh [--32] [--noble] [--appimage]" >&2; exit 1 ;;
    esac
done
if [ -n "$APPIMAGE" ] && [ -n "$SUFFIX" ]; then echo "--appimage is x86-64 only" >&2; exit 1; fi
if [ -n "$NOBLE" ] && [ -z "$SHAR_IN_NOBLE" ]; then
    podman build -q -t shar-noble -f "$ROOT/port/noble.Containerfile" "$ROOT/port" > /dev/null
    exec podman run --rm --userns=keep-id -e SHAR_IN_NOBLE=1 -v "$ROOT:$ROOT" -w "$ROOT" shar-noble port/package.sh "$@"
fi
if [ -n "$SUFFIX" ]; then ARCH=i386; X64=OFF; else ARCH=x86_64; X64=ON; fi
BUILD=$ROOT/build-linux$SUFFIX${NOBLE:+-noble}
NAME=shar-linux-$ARCH-$(date +%Y%m%d)
OUT=$ROOT/dist/$NAME

# (a build dir made for the other architecture, e.g. a 32-bit one from before x86-64 became the default, is switched)
if [ ! -f "$BUILD/build.ninja" ] || ! grep -q "^SHAR_64BIT:BOOL=$X64\$" "$BUILD/CMakeCache.txt"; then
    cmake -B "$BUILD" -S "$ROOT" -G Ninja -DSHAR_64BIT=$X64 > "$ROOT/port/build.log" 2>&1 || { tail -20 "$ROOT/port/build.log" >&2; exit 1; }
fi
ninja -C "$BUILD" -j"$(nproc)" > "$ROOT/port/build.log" 2>&1 || { grep -E "error|FAILED" "$ROOT/port/build.log" | head -20 >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/lib"
cp "$BUILD/srr2" "$OUT/srr2"
cp -r "$ROOT/shaders" "$OUT/shaders"

# Libraries: everything the game links against, except glibc (must be the system's: the loader is), and the graphics
# and windowing stack (libGL & co, libdrm/gbm, X11/xcb, Wayland), which has to match the target's driver.
SYSTEM='^(libc\.so|libm\.so|libmvec\.so|libresolv\.so|libpthread\.so|libdl\.so|librt\.so|ld-linux|linux-gate|libGL\.so|libGLX\.so|libGLdispatch\.so|libEGL\.so|libOpenGL\.so|libgbm\.so|libdrm\.so|libX|libxcb|libxkbcommon|libwayland|libdecor)'
ldd "$OUT/srr2" | awk '/=>/ { print $1, $3 }' | while read name path; do
    if ! echo "$name" | grep -qE "$SYSTEM"; then
        cp -L "$path" "$OUT/lib/$name"
    fi
done

cat > "$OUT/run.sh" <<'EOF'
#!/bin/bash
# Start the game. usage: ./run.sh [/path/to/game-data] [--mod MOD]... [game options...]
# game-data is your copy's PC data folder (holding art/, scripts/ and the .rcf files). The first time, a run/ folder
# is made next to this script with links to the data and copies of simpsons.ini and the saves (the game writes
# those); the shaders/ folder here is used too. Console output goes to run/logs/last-session.log.
# Extracted into the game data folder itself (no path given), the game runs there: no run/ folder, and it writes
# simpsons.ini, the saves and logs/ in that folder.
# --mod MOD loads a Mod Launcher mod: a .lmlm file or an extracted mod folder. Repeat it for several; when two mods
# change the same file, the one given first wins.
# As an AppImage (this script is its AppRun, on a read-only mount): the run folder is
# ~/.local/share/simpsons-hit-and-run, or the game data folder when the AppImage sits in it. With no terminal, a dialog
# asks for the game data folder.
HERE=$(cd "$(dirname "$0")" && pwd)
RUN=$HERE/run
HOMEDIR=$HERE
INAPPIMAGE=
if [ "$(basename "$0")" = AppRun ]; then
    INAPPIMAGE=1
    RUN=${XDG_DATA_HOME:-$HOME/.local/share}/simpsons-hit-and-run
    HOMEDIR=$(dirname "${APPIMAGE:-$HERE}")
fi
# An error, shown in a dialog too when an AppImage was started without a terminal
die() {
    echo "$1" >&2
    if [ -n "$INAPPIMAGE" ] && [ ! -t 2 ]; then
        { LD_LIBRARY_PATH= zenity --error --no-markup --text="$1" || LD_LIBRARY_PATH= kdialog --error "$1"; } 2>/dev/null
    fi
    exit 1
}
MODS=; ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --mod) [ $# -ge 2 ] || die "--mod needs a .lmlm file or a mod folder"; M=$2; shift 2 ;;
        --mod=*) M=${1#--mod=}; shift ;;
        *) ARGS+=("$1"); shift; continue ;;
    esac
    [ -e "$M" ] || die "Mod not found: $M"
    M=$(cd "$(dirname "$M")" && pwd)/$(basename "$M")
    case "$M" in *:*) die "Mod paths cannot contain ':': $M" ;; esac
    MODS=${MODS:+$MODS:}$M
done
set -- "${ARGS[@]}"
[ -n "$MODS" ] && export SHAR_MODS=$MODS
# Game data folder: holds art/ and scripts/ (any case)
has_data() { [ -n "$(find -L "$1" -maxdepth 1 -type d -iname art)" ] && [ -n "$(find -L "$1" -maxdepth 1 -type d -iname scripts)" ]; }
DATA=
if [ -n "$1" ] && [ -d "$1" ]; then DATA=$1; shift
elif has_data "$HOMEDIR"; then RUN=$HOMEDIR    # in the game data folder: run in place
elif [ -f "$RUN/.data" ]; then DATA=$(cat "$RUN/.data"); fi
if [ "$RUN" != "$HOMEDIR" ]; then
    if [ -n "$INAPPIMAGE" ] && [ ! -t 0 ] && { [ -z "$DATA" ] || [ ! -d "$DATA" ]; }; then
        T="Choose your Simpsons: Hit & Run game folder (holding art/, scripts/ and the .rcf files)"
        DATA=$({ zenity --file-selection --directory --title="$T" || kdialog --getexistingdirectory "$HOME" --title "$T"; } 2>/dev/null)
        [ -z "$DATA" ] || has_data "$DATA" || die "No art/ and scripts/ folders in $DATA"
    fi
    if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
        die "usage: $(basename "${APPIMAGE:-./run.sh}") /path/to/game-data [options]   (the folder holding art/, scripts/ and *.rcf)
       or put this package in the game data folder and run it there"
    fi
    DATA=$(cd "$DATA" && pwd)
    mkdir -p "$RUN"
    echo "$DATA" > "$RUN/.data"
    for f in ambience.rcf carsound.rcf dialog.rcf dialogf.rcf dialogg.rcf dialogs.rcf music00.rcf music01.rcf \
             music02.rcf music03.rcf nis.rcf scripts.rcf soundfx.rcf opening.bnr art movies scripts sound; do
        [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && ln -s "$DATA/$f" "$RUN/$f"
    done
    for f in simpsons.ini Save1 Save2 Save3 Save4; do
        [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && cp "$DATA/$f" "$RUN/$f"
    done
fi
# A link, not a copy, and renewed each start: an AppImage mounts somewhere new every time. Your own shaders/ folder stays.
if [ "$RUN" != "$HERE" ] && { [ -L "$RUN/shaders" ] || [ ! -e "$RUN/shaders" ]; }; then
    ln -sfn "$HERE/shaders" "$RUN/shaders"
fi
mkdir -p "$RUN/logs"
cd "$RUN"
# Only the bundled libraries the system lacks go on the path: a system library of the same name is ABI-compatible and
# at least as new, and the system's own plugins (GL driver, GTK for window decorations) need it, not an older copy.
LIBS=$RUN/.lib
rm -rf "$LIBS" && mkdir -p "$LIBS"
SYSLIBS=$({ ldconfig -p || /sbin/ldconfig -p; } 2>/dev/null | awk -v x64=@X64@ '/=>/ && (index($0, "x86-64") > 0) == x64 { print $1 }')
for f in "$HERE"/lib/*; do
    echo "$SYSLIBS" | grep -qxF "$(basename "$f")" || ln -s "$f" "$LIBS/"
done
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MISSING=$(ldd "$HERE/srr2" "$LIBS"/* 2>/dev/null | awk '/not found/ { print $1 }' | sort -u)
if [ -n "$MISSING" ]; then
    die "These system libraries are missing: $(echo $MISSING)
See the README for the packages to install."
fi
"$HERE/srr2" "$@" 2>&1 | tee "$RUN/logs/last-session.log"
EOF
if [ $ARCH = x86_64 ]; then X64=1; else X64=0; fi
sed -i "s/@X64@/$X64/" "$OUT/run.sh"
chmod +x "$OUT/run.sh"

# The newest glibc anything in the package needs (the target system's must be at least this)
GLIBC=$(for f in "$OUT/srr2" "$OUT"/lib/*; do objdump -T "$f"; done | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -Vu | tail -1)
if [ -n "$NOBLE" ] && [ "$(printf '%s\n' "$GLIBC" 2.39 | sort -V | tail -1)" != 2.39 ]; then
    echo "package needs glibc $GLIBC, more than Ubuntu 24.04's 2.39" >&2; exit 1
fi
if [ -n "$NOBLE" ]; then DISTRO="Ubuntu 24.04 / Linux Mint 22 (glibc $GLIBC)"; else DISTRO="one with glibc $GLIBC"; fi
if [ $ARCH = i386 ]; then
    NEEDS="Needs: a 64-bit Linux as new as $DISTRO, 32-bit graphics drivers and windowing libraries, and
your own copy of the PC game's data.

1. Install the 32-bit system libraries (Debian / Ubuntu):
     sudo dpkg --add-architecture i386 && sudo apt update
     sudo apt install libc6:i386 libgl1:i386 libgl1-mesa-dri:i386 libegl1:i386 libx11-6:i386 libxext6:i386 \\
         libxcursor1:i386 libxi6:i386 libxrandr2:i386 libxss1:i386 libxfixes3:i386 libxkbcommon0:i386 \\
         libwayland-client0:i386 libwayland-cursor0:i386 libwayland-egl1:i386 libdecor-0-0:i386 libxrender1:i386 \\
         libx11-xcb1:i386 libxcb-render0:i386 libxcb-shm0:i386 libxcb-dri3-0:i386 libgbm1:i386 libdrm2:i386
   With an NVIDIA card, also the driver's 32-bit libraries (e.g. libnvidia-gl-<version>:i386)."
else
    NEEDS="Needs: a 64-bit (x86-64) Linux as new as $DISTRO with a desktop (its graphics driver and
X11 / Wayland libraries, present on any desktop install), and your own copy of the PC game's data.

1. Nothing to install on a desktop system. On a minimal one (Debian / Ubuntu):
     sudo apt install libgl1 libgl1-mesa-dri libegl1 libx11-6 libxext6 libxcursor1 libxi6 libxrandr2 libxss1 \\
         libxfixes3 libxkbcommon0 libwayland-client0 libwayland-cursor0 libwayland-egl1 libdecor-0-0 libxrender1 \\
         libx11-xcb1 libxcb-render0 libxcb-shm0 libxcb-dri3-0 libgbm1 libdrm2"
fi
cat > "$OUT/README.txt" <<EOF
The Simpsons Hit & Run - Linux port, $ARCH (test build)

$NEEDS
2. Start it:  ./run.sh "/path/to/The Simpsons - Hit & Run"
   (afterwards just ./run.sh; options go after the path or on their own, e.g. ./run.sh skipfe)
   Or extract this package's contents into the game folder and run ./run.sh there: no run/ folder is made, and
   settings, saves and logs/ are written in the game folder.
   Mods (Mod Launcher .lmlm files or mod folders):  ./run.sh --mod MyMod.lmlm --mod OtherMod

Settings: run/simpsons.ini (#System: display, resolution, antialiasing, mipmaps, anisotropy).
Shaders: packs in shaders/ (see shaders/README.md): Options > Graphics > Shaders; M in the game turns them off and on.
A crash or problem: send run/logs/last-session.log.
EOF

tar -C "$ROOT/dist" -czf "$ROOT/dist/$NAME.tar.gz" "$NAME"
echo "$OUT"
echo "$ROOT/dist/$NAME.tar.gz ($(du -h "$ROOT/dist/$NAME.tar.gz" | cut -f1)), $(ls "$OUT/lib" | wc -l) libraries"

if [ -n "$APPIMAGE" ]; then
    # An AppImage is its runtime (mounts what follows it, needs fusermount but no libfuse) + a squashfs of the AppDir.
    RUNTIME_TAG=20251108
    RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
    RUNTIME=$ROOT/dist/.cache/appimage-runtime-$RUNTIME_TAG-x86_64
    if [ ! -f "$RUNTIME" ]; then
        mkdir -p "$(dirname "$RUNTIME")"
        curl -fsSL -o "$RUNTIME.part" "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-x86_64"
        mv "$RUNTIME.part" "$RUNTIME"
    fi
    echo "$RUNTIME_SHA256  $RUNTIME" | sha256sum -c --quiet - || { rm -f "$RUNTIME"; echo "AppImage runtime: bad checksum" >&2; exit 1; }

    APPDIR=$ROOT/dist/$NAME.AppDir
    rm -rf "$APPDIR"
    cp -r "$OUT" "$APPDIR"
    mv "$APPDIR/run.sh" "$APPDIR/AppRun"
    rm "$APPDIR/README.txt"
    cat > "$APPDIR/simpsons-hit-and-run.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=The Simpsons: Hit & Run
Exec=AppRun
Icon=simpsons-hit-and-run
Terminal=false
Categories=Game;
EOF
    cat > "$APPDIR/simpsons-hit-and-run.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
<rect width="256" height="256" rx="40" fill="#ffd90f"/>
<circle cx="128" cy="128" r="88" fill="#f48fb1"/><circle cx="128" cy="128" r="34" fill="#ffd90f"/>
</svg>
EOF
    ln -s simpsons-hit-and-run.svg "$APPDIR/.DirIcon"

    IMAGE=$ROOT/dist/$NAME.AppImage
    rm -f "$IMAGE.squashfs"
    mksquashfs "$APPDIR" "$IMAGE.squashfs" -all-root -noappend -no-progress -quiet -comp zstd -Xcompression-level 19 > /dev/null
    cat "$RUNTIME" "$IMAGE.squashfs" > "$IMAGE"
    rm -rf "$IMAGE.squashfs" "$APPDIR"
    chmod +x "$IMAGE"
    echo "$IMAGE ($(du -h "$IMAGE" | cut -f1))"
fi
