#!/bin/bash
# Builds SXE Core with CMake and installs it into Target/
#
# Usage:
#   ./comp_All.sh          optimized build (Release)
#   ./comp_All.sh debug    debug build (-O0 -g)
#   ./comp_All.sh test     build Release + test (CTest)
#   ./comp_All.sh clean    removes the CMakeWork/ folder (CMake work files)
#
# To build a single part: cmake --build CMakeWork --target Settings (Common, SXE, ...)

set -euo pipefail

# move to the project folder, wherever the script is launched from
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

CMAKE="${CMAKE:-cmake}"
BUILD_DIR="CMakeWork"   # CMake work files (objects, cache), only needed for fast rebuilds

if ! command -v "$CMAKE" >/dev/null 2>&1; then
    echo "Error: cmake not found (sudo apt install cmake)"
    exit 1
fi

case "${1:-}" in
    clean)
        rm -rf "$BUILD_DIR"
        echo "Folder $BUILD_DIR removed"
        exit 0
        ;;
    debug)
        BUILD_TYPE="Debug"
        ;;
    test)
        BUILD_TYPE="Release"
        RUN_TESTS=1
        ;;
    "")
        BUILD_TYPE="Release"
        ;;
    *)
        echo "Usage: $0 [debug|test|clean]"
        exit 1
        ;;
esac

echo "Build type: $BUILD_TYPE"

"$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
"$CMAKE" --build "$BUILD_DIR" -j"$(nproc)"
"$CMAKE" --install "$BUILD_DIR"

echo "Build completed: $DIR/Target/Bin"

if [ "${RUN_TESTS:-0}" = "1" ]; then
    CTEST="$(dirname "$(command -v "$CMAKE")")/ctest"
    "$CTEST" --test-dir "$BUILD_DIR" --output-on-failure -j"$(nproc)"
fi
