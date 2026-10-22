#!/usr/bin/env bash
# one-time setup for a McGuffin codespace: build tools, vcpkg, then a first build
set -e

sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config curl zip unzip tar \
    autoconf autoconf-archive automake libtool autopoint gettext gperf texinfo

if [ ! -d "$HOME/vcpkg" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
    "$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi

cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build
