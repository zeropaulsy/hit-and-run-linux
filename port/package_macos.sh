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

# The bundle's executable must be Mach-O to carry a signature: a stub that runs Resources/launch.sh. It starts AppKit
# only to receive the files the app was opened with (dropped on its icon, Open With, `open -a App file`): AppKit
# delivers them before applicationDidFinishLaunching, which then execs launch.sh with each file as --mod. Exec keeps
# the process, so the game runs as the app (Dock icon, Cmd+Q). Files dropped while the game runs are ignored.
cat > "$WORK/launch.m" <<'EOF'
#import <AppKit/AppKit.h>
#include <mach-o/dyld.h>
#include <libgen.h>
#include <limits.h>
static int gArgc;
static char** gArgv;
static NSMutableArray<NSString*>* gFiles;
@interface Launcher : NSObject <NSApplicationDelegate>
@end
@implementation Launcher
- (void)application:(NSApplication*)app openURLs:(NSArray<NSURL*>*)urls {
    for (NSURL* u in urls)
        if (u.isFileURL) [gFiles addObject:u.path];
}
- (void)applicationDidFinishLaunching:(NSNotification*)note {
    char exe[PATH_MAX], real[PATH_MAX], sh[PATH_MAX * 2];
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) || !realpath(exe, real)) exit(1);
    snprintf(sh, sizeof sh, "%s/../Resources/launch.sh", dirname(real));
    char** a = calloc(gArgc + 2 + 2 * gFiles.count, sizeof *a);
    int k = 0;
    a[k++] = "/bin/bash";
    a[k++] = sh;
    for (int i = 1; i < gArgc; i++) a[k++] = gArgv[i];
    for (NSString* f in gFiles) { a[k++] = "--mod"; a[k++] = strdup(f.fileSystemRepresentation); }
    execv(a[0], a);
    perror(sh);
    exit(1);
}
@end
int main(int argc, char** argv) {
    gArgc = argc;
    gArgv = argv;
    @autoreleasepool {
        gFiles = [NSMutableArray array];
        Launcher* d = [Launcher new];
        NSApplication.sharedApplication.delegate = d;
        [NSApp run];
    }
    return 1;
}
EOF
cc -O2 -fobjc-arc -arch "$ARCH" -mmacosx-version-min="$MINOS" -framework AppKit -o "$C/MacOS/launch" "$WORK/launch.m"

cat > "$C/Resources/launch.sh" <<'EOF'
#!/bin/bash
# Starts the game. The first time it asks for the PC game's data folder (holding art/, scripts/ and the .rcf files)
# and makes ~/Library/Application Support/Simpsons Hit & Run/ with links to it and copies of simpsons.ini and the
# saves (the game writes those) and of the shaders. Log: logs/last-session.log there. Hold Option while starting to
# pick another data folder. Arguments after `open -a "Simpsons Hit & Run" --args` go to the game, except
# --mod MOD (repeatable): a Mod Launcher .lmlm file or extracted mod folder, given as a full path (open -a starts the
# app in /, so relative paths can't be resolved); earlier mods win when two provide the same file (SHAR_MODS).
C=$(cd "$(dirname "$0")/.." && pwd)
HERE=$C/MacOS
APPNAME="Simpsons Hit & Run"
RUN=$HOME/Library/Application\ Support/$APPNAME
mkdir -p "$RUN/logs"

alert() { osascript -e "display alert \"$APPNAME\" message \"$1\" as critical" > /dev/null 2>&1; }
add_mod() {
    case "$1" in /*) ;; *) alert "Give mods as full paths (starting with / or ~): $1"; exit 1 ;; esac
    case "$1" in *:*) alert "Mod paths cannot contain a colon: $1"; exit 1 ;; esac
    [ -e "$1" ] || { alert "Mod not found: $1"; exit 1; }
    [ ! -d "$1" ] || [ -n "$(find "$1" -maxdepth 1 -iname meta.ini)" ] \
        || { alert "Not a mod folder (it has no Meta.ini): $1"; exit 1; }
    MODS=${MODS:+$MODS:}${1%/}
}
ARGS=()
MODS=
while [ $# -gt 0 ]; do
    case "$1" in
        -psn_*) shift ;;
        --mod) [ $# -ge 2 ] || { alert "--mod needs a .lmlm file or a mod folder."; exit 1; }; add_mod "$2"; shift 2 ;;
        --mod=*) add_mod "${1#--mod=}"; shift ;;
        *) ARGS+=("$1"); shift ;;
    esac
done

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
        simpsons.ini|save[1-8]|logs|crashes|shaders|vkpipelines.cache) continue ;;
    esac
    [ -e "$RUN/$n" ] || ln -s "$f" "$RUN/$n"
done
for f in simpsons.ini Save1 Save2 Save3 Save4 Save5 Save6 Save7 Save8; do
    [ -e "$DATA/$f" ] && [ ! -e "$RUN/$f" ] && cp "$DATA/$f" "$RUN/$f"
done
[ -e "$RUN/shaders" ] || cp -R "$C/Resources/shaders" "$RUN/shaders"

export SDL_VULKAN_LIBRARY=$C/Frameworks/libvulkan.1.dylib
export VK_DRIVER_FILES=$C/Resources/vulkan/icd.d/MoltenVK_icd.json
[ -z "$MODS" ] || export SHAR_MODS=$MODS
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
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>Mod Launcher mod</string>
            <key>CFBundleTypeRole</key><string>Viewer</string>
            <key>LSHandlerRank</key><string>Alternate</string>
            <key>CFBundleTypeExtensions</key><array><string>lmlm</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key><string>Mod folder</string>
            <key>CFBundleTypeRole</key><string>Viewer</string>
            <key>LSHandlerRank</key><string>Alternate</string>
            <key>LSItemContentTypes</key><array><string>public.folder</string></array>
        </dict>
    </array>
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

# Disk image: the app, a link to /Applications and an arrow between them on the window background. Finder lays the
# window out (AppleScript), so the first run may ask to let the terminal control Finder. The background is drawn at
# 2x on a square canvas (Quick Look renders SVGs square) and cropped, then paired with a 1x copy in one HiDPI TIFF.
VOL=$APPNAME
DMG=$DIST/shar-macos-$ARCH-$DATE.dmg
[ ! -e "/Volumes/$VOL" ] || { echo "/Volumes/$VOL is mounted: eject it first" >&2; exit 1; }
cat > "$WORK/background.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="1280">
<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4fa9e6"/><stop offset="1" stop-color="#9fd6f5"/></linearGradient>
  <filter id="sh" x="-20%" y="-20%" width="140%" height="160%"><feDropShadow dx="0" dy="2" stdDeviation="2.5" flood-color="#1d5a85" flood-opacity="0.35"/></filter>
</defs>
<g transform="translate(0 240) scale(2)">
  <rect width="640" height="400" fill="url(#sky)"/>
  <g fill="#fff" opacity="0.55">
    <ellipse cx="70" cy="330" rx="46" ry="18"/><ellipse cx="105" cy="318" rx="34" ry="20"/><ellipse cx="40" cy="322" rx="26" ry="14"/>
    <ellipse cx="560" cy="60" rx="44" ry="16"/><ellipse cx="590" cy="50" rx="30" ry="18"/><ellipse cx="530" cy="54" rx="24" ry="12"/>
    <ellipse cx="420" cy="350" rx="36" ry="13"/><ellipse cx="445" cy="342" rx="24" ry="14"/>
  </g>
  <text x="320" y="68" font-family="-apple-system, Helvetica Neue, Helvetica" font-size="24" font-weight="700" fill="#fff" text-anchor="middle" filter="url(#sh)">Drag the game into Applications</text>
  <path d="M252 190 H362 V170 L400 200 L362 230 V210 H252 Z" fill="#fff" stroke="#fff" stroke-width="4" stroke-linejoin="round" filter="url(#sh)"/>
  <text x="320" y="368" font-family="-apple-system, Helvetica Neue, Helvetica" font-size="13" font-weight="500" fill="#fff" text-anchor="middle" filter="url(#sh)">First start blocked? System Settings › Privacy &amp; Security › Open Anyway</text>
</g>
</svg>
EOF
rm -f "$WORK/background.svg.png"
qlmanage -t -s 1280 -o "$WORK" "$WORK/background.svg" 2>&1 | grep -q "produced one thumbnail" \
    || { echo "Quick Look did not render the disk image background" >&2; exit 1; }
sips -c 800 1280 "$WORK/background.svg.png" --out "$WORK/background@2x.png" > /dev/null
sips -z 400 640 "$WORK/background@2x.png" --out "$WORK/background.png" > /dev/null
tiffutil -cathidpicheck "$WORK/background.png" "$WORK/background@2x.png" -out "$WORK/background.tiff" 2> /dev/null

STAGE=$(mktemp -d)
ditto "$APP" "$STAGE/$APPNAME.app"
ln -s /Applications "$STAGE/Applications"
mkdir "$STAGE/.background"
cp "$WORK/background.tiff" "$STAGE/.background/background.tiff"
RW=$WORK/rw.dmg
rm -f "${RW:?}" "${DMG:?}"
hdiutil create -quiet -srcfolder "$STAGE" -volname "$VOL" -fs HFS+ -format UDRW -size "$(( $(du -sm "$STAGE" | cut -f1) + 20 ))m" "$RW"
rm -rf "${STAGE:?}"
DEV=$(hdiutil attach -readwrite -noverify -noautoopen "$RW" | awk '/Apple_HFS/ { print $1 }')
[ -n "$DEV" ] && [ -d "/Volumes/$VOL" ] || { echo "could not mount $RW" >&2; exit 1; }
osascript <<EOF
tell application "Finder"
    tell disk "$VOL"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 840, 548}    -- 640x400 below the 28-point title bar
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 128
        set text size of opts to 13
        set background picture of opts to file ".background:background.tiff"
        set position of item "$APPNAME.app" of container window to {160, 200}
        set position of item "Applications" of container window to {480, 200}
        update without registering applications
        delay 1
        close
    end tell
end tell
EOF
# Volume icon after the layout: Finder deletes a .VolumeIcon.icns that is there while it writes the window settings
cp "$C/Resources/icon.icns" "/Volumes/$VOL/.VolumeIcon.icns"
SetFile -a C "/Volumes/$VOL"    # custom icon flag (SetFile comes with the Command Line Tools)
sync
rm -rf "/Volumes/$VOL/.fseventsd"
hdiutil detach -quiet "$DEV" || { sleep 2; hdiutil detach -quiet -force "$DEV"; }
hdiutil convert -quiet "$RW" -format ULFO -o "$DMG"
rm -f "${RW:?}"
codesign --force --sign - "$DMG"

echo "$APP"
echo "$ZIP ($(du -h "$ZIP" | cut -f1)), macOS $MINOS+, symbols in ${SYM#$ROOT/}"
echo "$DMG ($(du -h "$DMG" | cut -f1))"
