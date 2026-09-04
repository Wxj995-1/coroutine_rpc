#!/bin/bash
set -e

BUILD_DIR="build"

# 第一个参数处理
case "${1:-}" in
  clean)
    rm -rf "$BUILD_DIR"
    exit 0
    ;;
  debug|Debug|DEBUG)
    BUILD_TYPE="Debug"
    ;;
  release|Release|RELEASE)
    BUILD_TYPE="Release"
    ;;
  *)
    BUILD_TYPE="Debug"
    ;;
esac

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build . -- -j"$(nproc)"
