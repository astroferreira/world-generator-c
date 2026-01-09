#!/bin/bash

set -e

BUILD_DIR="build"
BUILD_TYPE="Release"
CLEAN=false
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -c, --clean     Clean build (remove build directory first)"
    echo "  -d, --debug     Build in Debug mode"
    echo "  -r, --release   Build in Release mode (default)"
    echo "  -j N            Number of parallel jobs (default: $JOBS)"
    echo "  -h, --help      Show this help message"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

cd "$(dirname "$0")"

if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring ($BUILD_TYPE)..."
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..

echo "Building with $JOBS jobs..."
cmake --build . -j "$JOBS"

echo ""
echo "Build complete! Executable: $BUILD_DIR/worldgen"
