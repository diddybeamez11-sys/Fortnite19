#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# E-CLIENT BUILD + PATCH
# Minecraft Bedrock 1.21.111 ARM64
# ============================================================

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INPUT_APK="${1:-}"
OUTPUT_APK="${2:-$ROOT/out/eclient-minecraft-1.21.111-arm64.apk}"

BUILD_DIR="$ROOT/build/native-android"
HOST_SO="$BUILD_DIR/runtime/libeclient_host.so"

PATCHER="$ROOT/tools/patch-minecraft-apk.sh"

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

echo "=========================================="
echo " E-CLIENT BUILD + PATCH"
echo " Minecraft 1.21.111 ARM64"
echo "=========================================="
echo

# ------------------------------------------------------------
# Validate arguments
# ------------------------------------------------------------

if [[ -z "$INPUT_APK" ]]; then
    echo "ERROR: Missing Minecraft APK."
    echo
    echo "Usage:"
    echo "  $0 <minecraft.apk> [output.apk]"
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: Minecraft APK does not exist:"
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

# ------------------------------------------------------------
# 1. Verify Minecraft APK
# ------------------------------------------------------------

echo "[1/4] Verifying Minecraft APK..."

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected:"
echo "$EXPECTED_SHA256"
echo
echo "Actual:"
echo "$ACTUAL_SHA256"
echo

if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
    echo "ERROR: Minecraft APK SHA-256 mismatch."
    exit 1
fi

echo "Minecraft APK verified."
echo

# ------------------------------------------------------------
# 2. Configure native build
# ------------------------------------------------------------

echo "[2/4] Configuring native build..."

cmake \
    -S "$ROOT/native/runtime" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DANDROID_NDK="${ANDROID_NDK_HOME:-}" \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"

echo

# ------------------------------------------------------------
# 3. Build native runtime
# ------------------------------------------------------------

echo "[3/4] Building native runtime..."

cmake \
    --build "$BUILD_DIR" \
    --target eclient_host \
    --parallel

echo

if [[ ! -f "$HOST_SO" ]]; then
    echo "ERROR: Native host library was not produced:"
    echo "$HOST_SO"
    exit 1
fi

echo "Native host:"
ls -lh "$HOST_SO"
echo

# ------------------------------------------------------------
# Verify native library architecture
# ------------------------------------------------------------

if ! file "$HOST_SO" | grep -Eq \
    'ELF 64-bit.*ARM aarch64|ELF 64-bit.*aarch64'; then

    echo "ERROR: libeclient_host.so is not ARM64."
    exit 1
fi

echo "ARM64 native library verified."
echo

# ------------------------------------------------------------
# 4. Patch Minecraft APK
# ------------------------------------------------------------

echo "[4/4] Patching Minecraft APK..."

"$PATCHER" \
    "$INPUT_APK" \
    "$HOST_SO" \
    "$OUTPUT_APK"

echo
echo "=========================================="
echo " E-CLIENT BUILD COMPLETE"
echo "=========================================="
echo
echo "Output APK:"
echo "$OUTPUT_APK"
echo

if [[ -f "$OUTPUT_APK" ]]; then
    ls -lh "$OUTPUT_APK"
else
    echo "ERROR: Output APK was not created."
    exit 1
fi
