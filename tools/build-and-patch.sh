#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT_APK="${1:?Usage: build-and-patch.sh <minecraft-1.21.111-arm64.apk> [output.apk]}"
OUTPUT_APK="${2:-$ROOT/eclient-minecraft-1.21.111-arm64.apk}"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"
[[ -n "$NDK" && -f "$NDK/build/cmake/android.toolchain.cmake" ]] || { echo "ANDROID_NDK_HOME must point to an Android NDK" >&2; exit 2; }
EXPECTED_SHA="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"
ACTUAL_SHA="$(sha256sum "$INPUT_APK" | awk '{print $1}')"
if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
  echo "Refusing to patch: expected the supplied Minecraft 1.21.111 ARM64 APK." >&2
  echo "Expected: $EXPECTED_SHA" >&2; echo "Actual:   $ACTUAL_SHA" >&2; exit 3
fi
cmake -S "$ROOT/native" -B "$ROOT/build/native-android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
cmake --build "$ROOT/build/native-android" --parallel
HOST_SO="$ROOT/build/native-android/runtime/libeclient_host.so"
[[ -f "$HOST_SO" ]] || { echo "libeclient_host.so was not produced" >&2; exit 4; }
"$ROOT/tools/patch-minecraft-apk.sh" "$INPUT_APK" "$HOST_SO" "$OUTPUT_APK"
echo "Ready: $OUTPUT_APK"
