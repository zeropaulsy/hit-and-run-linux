#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Package the macOS build as an app: dist/macos/Simpsons Hit & Run.app and a zip of it,
# shar-macos-<arch>-<date>.zip, holding the game, the Homebrew libraries it needs (SDL2 + SDL3, OpenAL, FFmpeg and
# theirs, the Vulkan loader and MoltenVK), the example shaders and a launcher. Signed ad hoc (no Developer ID): a
# downloaded copy has to be allowed once in System Settings > Privacy & Security (see the README in the zip).
# The unstripped binary + dSYM go to dist/symbols/macos-<arch>-<UUID>/ for crash reports.
# usage: port/package_macos.sh [--no-build]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BREW=${SHAR_BREW:-/opt/homebrew}
export PATH=$BREW/bin:$PATH
BUILD=$ROOT/build-macos
APPNAME="Simpsons Hit & Run"
ARCH=$(uname -m)
DATE=$(date +%Y%m%d)
DIST=$ROOT/dist/macos
APP=$DIST/$APPNAME.app
C=$APP/Contents
WORK=$DIST/work    # launcher source and icon files, overwritten each run

if [ "$1" != --no-build ]; then
    [ -f "$BUILD/build.ninja" ] || cmake -S "$ROOT" -B "$BUILD" -G Ninja > "$ROOT/port/build.log" 2>&1 \
        || { tail -20 "$ROOT/port/build.log" >&2; exit 1; }
    ninja -C "$BUILD" -j"$(sysctl -n hw.ncpu)" > "$ROOT/port/build.log" 2>&1 \
        || { grep -E "error|FAILED" "$ROOT/port/build.log" | head -20 >&2; exit 1; }
fi
grep -q '^CMAKE_BUILD_TYPE:STRING=\(RelWithDebInfo\|Release\)$' "$BUILD/CMakeCache.txt" \
    || { echo "$BUILD is not an optimised build" >&2; exit 1; }

rm -rf "${APP:?}"
mkdir -p "$WORK/icon.iconset" "$C/MacOS" "$C/Frameworks" "$C/Resources/vulkan/icd.d"
cp "$BUILD/srr2" "$C/MacOS/Simpsons"
cp -R "$ROOT/shaders" "$C/Resources/shaders"
find "$C/Resources" -name .DS_Store -delete

# Libraries. Run-time loaded ones: sdl2-compat dlopens SDL3 (@loader_path/libSDL3.dylib), SDL dlopens the Vulkan
# loader (SDL_VULKAN_LIBRARY, set by the launcher), which loads MoltenVK through the ICD file below.
MINOS=$(python3 "$ROOT/port/macos_bundle_libs.py" "$C/Frameworks" "$C/MacOS/Simpsons" \
    "$BREW/opt/sdl3/lib/libSDL3.0.dylib=libSDL3.dylib" \
    "$BREW/opt/vulkan-loader/lib/libvulkan.1.dylib" \
    "$BREW/opt/molten-vk/lib/libMoltenVK.dylib")
cat > "$C/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'EOF'
{
    "file_format_version" : "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version" : "1.4.0",
        "is_portability_driver" : true
    }
}
EOF

# Symbols, then strip the shipped binary
UUID=$(dwarfdump --uuid "$C/MacOS/Simpsons" | awk '{ print $2; exit }')
SYM=$ROOT/dist/symbols/macos-$ARCH-$UUID
rm -rf "${SYM:?}" && mkdir -p "$SYM"
cp "$C/MacOS/Simpsons" "$SYM/Simpsons"
dsymutil "$SYM/Simpsons" -o "$SYM/Simpsons.dSYM" 2> /dev/null
strip -S -x "$C/MacOS/Simpsons"

# The bundle's executable must be Mach-O to carry a signature: a stub that runs Resources/launch.sh
printf '%s\n' '#include <mach-o/dyld.h>' '#include <libgen.h>' '#include <limits.h>' '#include <stdio.h>' \
    '#include <stdlib.h>' '#include <unistd.h>' \
    'int main(int argc, char** argv) { char exe[PATH_MAX], real[PATH_MAX], sh[PATH_MAX * 2]; uint32_t n = sizeof exe;' \
    '  if (_NSGetExecutablePath(exe, &n) || !realpath(exe, real)) return 1;' \
    '  snprintf(sh, sizeof sh, "%s/../Resources/launch.sh", dirname(real));' \
    '  char** a = calloc(argc + 2, sizeof *a); a[0] = "/bin/bash"; a[1] = sh;' \
    '  for (int i = 1; i < argc; i++) a[i + 1] = argv[i];' \
    '  execv(a[0], a); perror(sh); return 1; }' > "$WORK/launch.c"
cc -O2 -arch "$ARCH" -mmacosx-version-min="$MINOS" -o "$C/MacOS/launch" "$WORK/launch.c"

cat > "$C/Resources/launch.sh" <<'EOF'
#!/bin/bash
# Starts the game. The first time it asks for the PC game's data folder (holding art/, scripts/ and the .rcf files)
# and makes ~/Library/Application Support/Simpsons Hit & Run/ with links to it and copies of simpsons.ini and the
# saves (the game writes those) and of the shaders. Log: logs/last-session.log there. Hold Option while starting to
# pick another data folder. Arguments after `open -a "Simpsons Hit & Run" --args` go to the game.
C=$(cd "$(dirname "$0")/.." && pwd)
HERE=$C/MacOS
APPNAME="Simpsons Hit & Run"
RUN=$HOME/Library/Application\ Support/$APPNAME
mkdir -p "$RUN/logs"
ARGS=()
for a in "$@"; do case "$a" in -psn_*) ;; *) ARGS+=("$a") ;; esac; done

alert() { osascript -e "display alert \"$APPNAME\" message \"$1\" as critical" > /dev/null 2>&1; }
has_data() { [ -n "$(find -L "$1" -maxdepth 1 -type d -iname art)" ] && [ -n "$(find -L "$1" -maxdepth 1 -type d -iname scripts)" ]; }
option_held() {
    osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & $.NSEventModifierFlagOption) ? "1" : ""' 2> /dev/null
}

DATA=
[ -f "$RUN/.data" ] && DATA=$(cat "$RUN/.data")
if [ -z "$(option_held)" ] && [ -n "$DATA" ] && has_data "$DATA"; then :
elif has_data "$(dirname "$(dirname "$C")")" && [ -z "$DATA" ]; then DATA=$(dirname "$(dirname "$C")")
else
    PROMPT="Choose your PC game folder for The Simpsons: Hit & Run (the one holding art, scripts and the .rcf files)."
    while :; do
        DATA=$(osascript -e "POSIX path of (choose folder with prompt \"$PROMPT\")" 2> /dev/null) || exit 0
        DATA=${DATA%/}
        has_data "$DATA" && break
        PROMPT="That folder has no art and scripts folders. Choose the PC game folder (the one holding art, scripts and the .rcf files)."
    done
fi
echo "$DATA" > "$RUN/.data"

# Links to the data (replaced when the data folder changes), copies of what the game writes
for f in "${RUN:?}"/*; do [ -L "$f" ] && rm "$f"; done
for f in "$DATA"/*; do
    n=$(basename "$f")
    case "$(echo "$n" | tr A-Z a-z)" in
        simpsons.ini|save[1-4]|logs|crashes|shaders|vkpipelines.cache) continue ;;
    esac
    [ -e "$RUN/$n" ] || ln -s "$f" "$RUN/$n"
done
for f in simpsons.ini Save1 Save2 Save3 Save4; do
    [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && cp "$DATA/$f" "$RUN/$f"
done
[ -e "$RUN/shaders" ] || cp -R "$C/Resources/shaders" "$RUN/shaders"

export SDL_VULKAN_LIBRARY=$C/Frameworks/libvulkan.1.dylib
export VK_DRIVER_FILES=$C/Resources/vulkan/icd.d/MoltenVK_icd.json
cd "$RUN"
exec "$HERE/Simpsons" "${ARGS[@]}" > "$RUN/logs/last-session.log" 2>&1
EOF
chmod +x "$C/Resources/launch.sh"

# Icon: the AppImage's (package.sh on dev), inside the macOS icon grid (824 of 1024, centred), rendered by Quick Look
cat > "$WORK/icon.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024"><g transform="translate(100 100) scale(3.21875)">
<rect width="256" height="256" rx="40" fill="#ffd90f"/>
<circle cx="128" cy="128" r="88" fill="#f48fb1"/><circle cx="128" cy="128" r="34" fill="#ffd90f"/>
</g></svg>
EOF
qlmanage -t -s 1024 -o "$WORK" "$WORK/icon.svg" 2>&1 | grep -q "produced one thumbnail" \
    || { echo "Quick Look did not render the icon" >&2; exit 1; }
for s in 16 32 128 256 512; do
    sips -z $s $s "$WORK/icon.svg.png" --out "$WORK/icon.iconset/icon_${s}x${s}.png" > /dev/null
    sips -z $((s * 2)) $((s * 2)) "$WORK/icon.svg.png" --out "$WORK/icon.iconset/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$WORK/icon.iconset" -o "$C/Resources/icon.icns"

XMLNAME=${APPNAME//&/&amp;}
cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$XMLNAME</string>
    <key>CFBundleDisplayName</key><string>$XMLNAME</string>
    <key>CFBundleIdentifier</key><string>org.shar-port.hitandrun</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundleIconFile</key><string>icon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.$DATE</string>
    <key>CFBundleVersion</key><string>$DATE</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>LSMinimumSystemVersion</key><string>$MINOS</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
EOF
plutil -lint -s "$C/Info.plist"

# Ad hoc signature: install_name_tool invalidated the libraries' signatures, and arm64 refuses unsigned code
for f in "$C"/Frameworks/*.dylib "$C/MacOS/Simpsons" "$C/MacOS/launch"; do codesign --force --sign - "$f" 2> /dev/null; done
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

ZIP=$DIST/shar-macos-$ARCH-$DATE.zip
rm -f "${ZIP:?}"
STAGE=$(mktemp -d)
ditto "$APP" "$STAGE/$APPNAME.app"
cp "$ROOT/port/dist_README_macos.md" "$STAGE/README.md"
(cd "$STAGE" && ditto -c -k --sequesterRsrc . "$ZIP")
rm -rf "${STAGE:?}"
echo "$APP"
echo "$ZIP ($(du -h "$ZIP" | cut -f1)), macOS $MINOS+, symbols in ${SYM#$ROOT/}"
