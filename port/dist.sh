#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Make the x86-64 release tarball for the GitHub Releases page: dist/release/shar-linux-x86_64-<date>.tar.gz
#
# It's the `package.sh --noble` bundle with the binary stripped and renamed Simpsons, and README.md from
# port/dist_README.md. The unstripped copy goes to dist/symbols/x86_64-<build ID>/ for crash_symbolize.py. The 32-bit
# tarball is shared separately. Nothing is committed: upload the tarball yourself.
#
# usage: port/dist.sh [--no-build]    (--no-build: reuse the newest x86-64 bundle in dist/)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
[ "$1" = --no-build ] || port/package.sh --noble > /dev/null
SRC=$(ls -d dist/shar-linux-x86_64-[0-9]* | grep -v '\.tar\.gz$' | sort | tail -1)
[ -n "$SRC" ] || { echo "no x86_64 bundle in dist/" >&2; exit 1; }
NAME=$(basename "$SRC")
OUT=dist/release/$NAME

rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$SRC/run.sh" "$SRC/shaders" "$SRC/lib" "$OUT/"
cp "$SRC/srr2" "$OUT/Simpsons"
strip "$OUT/Simpsons"
ID=$(readelf -n "$SRC/srr2" | awk '/Build ID/ { print $3 }')
mkdir -p "dist/symbols/x86_64-$ID"
cp "$SRC/srr2" "dist/symbols/x86_64-$ID/Simpsons"

sed -i 's|"\$HERE/srr2"|"$HERE/Simpsons"|g' "$OUT/run.sh"
! grep -q srr2 "$OUT/run.sh" || { echo "run.sh: binary not renamed" >&2; exit 1; }
cp port/dist_README.md "$OUT/README.md"

tar -C dist/release --owner=0 --group=0 --numeric-owner -czf "$OUT.tar.gz" "$NAME"
echo "$OUT.tar.gz ($(du -h "$OUT.tar.gz" | cut -f1)), build ID $ID"
