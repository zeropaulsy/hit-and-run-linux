# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Ubuntu 24.04 build environment for release packages (port/package.sh --noble). Building against 24.04's glibc (2.39)
# and libraries lets the package run on Ubuntu 24.04 / Linux Mint 22 and anything newer.
FROM docker.io/library/ubuntu:24.04
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        cmake ninja-build python3 build-essential gcc-multilib g++-multilib file binutils \
        libsdl2-dev libgl-dev libopenal-dev libvulkan-dev \
        libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev \
        libsdl2-dev:i386 libgl-dev:i386 libopenal-dev:i386 \
        libavformat-dev:i386 libavcodec-dev:i386 libswscale-dev:i386 libswresample-dev:i386 libavutil-dev:i386 \
    && rm -rf /var/lib/apt/lists/*
