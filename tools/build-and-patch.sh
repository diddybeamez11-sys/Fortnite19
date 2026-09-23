#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

INPUT_APK="${1:?Usage: build-and-patch.sh <minecraft-1.21.111-arm64.apk> [output.apk]}"
OUTPUT_APK="${2:-$ROOT/eclient-minecraft-1.21.111-arm64.apk}"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"

EXPECTED_SHA="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

if [[ -z "$NDK" || ! -f "$NDK/build/cmake/android.toolchain.cmake" ]]; then
    echo "ERROR: ANDROID_NDK_HOME must point to an Android NDK"
    exit 2
fi

echo "=========================================="
echo " E-CLIENT BUILD + PATCH"
echo " Minecraft 1.21.111 ARM64"
echo "=========================================="

echo
echo "[1/4] Verifying Minecraft APK..."

ACTUAL_SHA="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected: $EXPECTED_SHA"
echo "Actual:   $ACTUAL_SHA"

if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
    echo "ERROR: Wrong Minecraft APK."
    exit 3
fi

echo "Minecraft APK verified."

echo
echo "[2/4] Configuring native build..."

cmake \
    -S "$ROOT/native" \
    -B "$ROOT/build/native-android" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26

echo
echo "[3/4] Building native runtime..."

cmake \
    --build "$ROOT/build/native-android" \
    --parallel 2 \
    --verbose

HOST_SO="$ROOT/build/native-android/runtime/libeclient_host.so"

if [[ ! -f "$HOST_SO" ]]; then
    echo
    echo "ERROR: libeclient_host.so was not produced."
    echo "Expected:"
    echo "$HOST_SO"
    echo
    echo "Produced libraries:"
    find "$ROOT/build/native-android" \
        -type f \
        -name "*.so" \
        -print || true
    exit 4
fi

echo
echo "Native host:"
ls -lh "$HOST_SO"

echo
echo "[4/4] Patching Minecraft APK..."

"$ROOT/tools/patch-minecraft-apk.sh" \
    "$INPUT_APK" \
    "$HOST_SO" \
    "$OUTPUT_APK"

if [[ ! -s "$OUTPUT_APK" ]]; then
    echo "ERROR: Output APK was not produced."
    exit 5
fi

echo
echo "=========================================="
echo " BUILD SUCCESSFUL"
echo "=========================================="
echo
echo "Output:"
echo "$OUTPUT_APK"
echo
ls -lh "$OUTPUT_APK"
echo
echo "SHA-256:"
sha256sum "$OUTPUT_APK"
