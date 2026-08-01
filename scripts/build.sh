#!/bin/sh
set -e

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"

echo "[*] Configuring..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "[*] Building..."
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "[*] Build complete: ${BUILD_DIR}/gs"
echo "[*] Install with: sudo cp ${BUILD_DIR}/gs /usr/local/bin/gs"
