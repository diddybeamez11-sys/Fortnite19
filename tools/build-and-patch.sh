#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INPUT_APK="${1:-}"
OUTPUT_APK="${2:-$ROOT/out/eclient-minecraft-1.21.111-arm64.apk}"

BUILD_DIR="$ROOT/build/native-android"
HOST_SO="$BUILD_DIR/runtime/libeclient_host.so"
PATCHER="$ROOT/tools/patch-minecraft-apk.sh"

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

if [[ -z "$INPUT_APK" ]]; then
    echo "ERROR: Missing Minecraft APK."
    echo "Usage:"
    echo "  $0 <minecraft-apk> [output-apk]"
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: Input APK does not exist:"
    echo "$INPUT_APK"
    exit 1
fi

if [[ ! -f "$PATCHER" ]]; then
    echo "ERROR: APK patcher does not exist:"
    echo "$PATCHER"
    exit 1
fi

chmod +x "$PATCHER"

mkdir -p "$ROOT/out"

echo "=========================================="
echo " E-CLIENT BUILD + PATCH"
echo " Minecraft 1.21.111 ARM64"
echo "=========================================="

echo
echo "[1/4] Verifying Minecraft APK..."

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected:"
echo "$EXPECTED_SHA256"
echo
echo "Actual:"
echo "$ACTUAL_SHA256"

if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
    echo
    echo "ERROR: Minecraft APK SHA-256 does not match."
    exit 1
fi

echo "Minecraft APK verified."

echo
echo "[2/4] Configuring native build..."

# ------------------------------------------------------------------
# IMPORTANT:
# Always remove the previous native CMake build directory.
#
# The project previously used:
#     native/CMakeLists.txt
#
# The current runtime uses:
#     native/runtime/CMakeLists.txt
#
# Reusing the old CMakeCache.txt causes:
#
#   CMake Error:
#   The source ".../native/runtime/CMakeLists.txt"
#   does not match the source ".../native/CMakeLists.txt"
#
# A clean native build directory prevents stale CMake state.
# ------------------------------------------------------------------

if [[ -d "$BUILD_DIR" ]]; then
    echo "Removing stale native CMake build directory:"
    echo "$BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# Find the Android NDK reliably in GitHub Actions.
NDK_PATH="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"

if [[ -z "$NDK_PATH" ]]; then
    echo "ERROR: ANDROID_NDK_HOME / ANDROID_NDK_ROOT is not set."
    exit 1
fi

if [[ ! -d "$NDK_PATH" ]]; then
    echo "ERROR: Android NDK directory does not exist:"
    echo "$NDK_PATH"
    exit 1
fi

TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake"

if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "ERROR: Android NDK CMake toolchain was not found:"
    echo "$TOOLCHAIN_FILE"
    exit 1
fi

echo "Native source:"
echo "$ROOT/native/runtime"

echo "Native build:"
echo "$BUILD_DIR"

echo "Android NDK:"
echo "$NDK_PATH"

cmake \
    -S "$ROOT/native/runtime" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"

echo
echo "[3/4] Building native runtime..."

cmake \
    --build "$BUILD_DIR" \
    --target eclient_host \
    --parallel

if [[ ! -f "$HOST_SO" ]]; then
    echo
    echo "ERROR: Native host library was not produced:"
    echo "$HOST_SO"
    exit 1
fi

echo
echo "Native host:"
ls -lh "$HOST_SO"

echo
echo "Checking native library architecture..."

FILE_OUTPUT="$(file "$HOST_SO")"

echo "$FILE_OUTPUT"

if ! echo "$FILE_OUTPUT" | grep -qiE 'ELF.*aarch64|ARM aarch64|ARM64'; then
    echo
    echo "ERROR: Native host library is not ARM64."
    exit 1
fi

echo "ARM64 native library verified."

echo
echo "[4/4] Patching Minecraft APK..."

"$PATCHER" \
    "$INPUT_APK" \
    "$HOST_SO" \
    "$OUTPUT_APK"

if [[ ! -f "$OUTPUT_APK" ]]; then
    echo
    echo "ERROR: APK patcher completed without producing:"
    echo "$OUTPUT_APK"
    exit 1
fi

echo
echo "=========================================="
echo " BUILD COMPLETE"
echo "=========================================="
echo
echo "Output APK:"
ls -lh "$OUTPUT_APK"
echo
echo "SHA-256:"
sha256sum "$OUTPUT_APK"
echo
