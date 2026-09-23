#!/usr/bin/env bash

set -euo pipefail

APK="${1:-}"

if [[ -z "$APK" ]]; then
    echo "Usage:"
    echo "  $0 <minecraft-1.21.111.apk>"
    exit 1
fi

if [[ ! -f "$APK" ]]; then
    echo "ERROR: APK does not exist:"
    echo "$APK"
    exit 2
fi

EXPECTED_SHA="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

EXPECTED_BUILD="ea5614dcc3551721b14f9b4686aa1651c1b8f79e"

TMP="$(mktemp -d)"

cleanup() {
    rm -rf "$TMP"
}

trap cleanup EXIT

LIB="$TMP/libminecraftpe.so"

# ==============================================================
# APK SHA
# ==============================================================

echo "=========================================="
echo " Checking Minecraft APK"
echo "=========================================="

ACTUAL_SHA="$(sha256sum "$APK" | awk '{print $1}')"

echo
echo "APK:"
echo "$APK"

echo
echo "Actual SHA:"
echo "$ACTUAL_SHA"

echo
echo "Expected SHA:"
echo "$EXPECTED_SHA"

if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
    echo
    echo "ERROR: Minecraft APK SHA-256 mismatch."
    exit 3
fi

echo
echo "APK SHA verified."

# ==============================================================
# EXTRACT LIBMINECRAFTPE
# ==============================================================

echo
echo "=========================================="
echo " Extracting libminecraftpe.so"
echo "=========================================="

if ! unzip -p \
    "$APK" \
    'lib/arm64-v8a/libminecraftpe.so' \
    > "$LIB"; then

    echo "ERROR: Failed to extract libminecraftpe.so."
    exit 4
fi

if [[ ! -s "$LIB" ]]; then
    echo "ERROR: libminecraftpe.so is empty or missing."
    exit 5
fi

echo "Extracted:"
ls -lh "$LIB"

# ==============================================================
# ARCHITECTURE
# ==============================================================

echo
echo "=========================================="
echo " Checking architecture"
echo "=========================================="

file "$LIB"

if ! file "$LIB" | grep -qiE \
    'ARM aarch64|ARM64'; then

    echo
    echo "ERROR: libminecraftpe.so is not ARM64."
    exit 6
fi

echo
echo "ARM64 verified."

# ==============================================================
# FIND LLVM READELF
# ==============================================================

echo
echo "=========================================="
echo " Locating llvm-readelf"
echo "=========================================="

READELF=""

CANDIDATES=(
    "${ANDROID_NDK_HOME:-}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    "${ANDROID_NDK_ROOT:-}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    "${ANDROID_SDK_ROOT:-}/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    "$(command -v llvm-readelf 2>/dev/null || true)"
)

for candidate in "${CANDIDATES[@]}"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
        READELF="$candidate"
        break
    fi
done

if [[ -z "$READELF" ]]; then
    echo "ERROR: llvm-readelf was not found."
    exit 7
fi

echo "Using:"
echo "$READELF"

# ==============================================================
# GNU BUILD ID
# ==============================================================

echo
echo "=========================================="
echo " Reading GNU Build ID"
echo "=========================================="

NOTE_OUTPUT="$TMP/readelf-notes.txt"

"$READELF" \
    -n "$LIB" \
    > "$NOTE_OUTPUT" 2>&1

echo
echo "GNU notes:"
cat "$NOTE_OUTPUT"

ACTUAL_BUILD="$(
    sed -nE \
        's/^[[:space:]]*Build ID:[[:space:]]*([0-9A-Fa-f]+).*/\1/p' \
        "$NOTE_OUTPUT" |
    head -n 1 |
    tr '[:upper:]' '[:lower:]'
)"

echo
echo "Actual Build ID:"
echo "${ACTUAL_BUILD:-<empty>}"

echo
echo "Expected Build ID:"
echo "$EXPECTED_BUILD"

# ==============================================================
# BUILD ID VALIDATION
# ==============================================================

if [[ -z "$ACTUAL_BUILD" ]]; then
    echo
    echo "ERROR: GNU Build ID could not be read."
    echo
    echo "ELF note output was:"
    cat "$NOTE_OUTPUT"
    exit 8
fi

if [[ "$ACTUAL_BUILD" != "$EXPECTED_BUILD" ]]; then
    echo
    echo "ERROR: GNU Build ID mismatch."
    echo
    echo "Actual:"
    echo "$ACTUAL_BUILD"
    echo
    echo "Expected:"
    echo "$EXPECTED_BUILD"
    exit 9
fi

# ==============================================================
# SUCCESS
# ==============================================================

echo
echo "=========================================="
echo " Minecraft 1.21.111 VERIFIED"
echo "=========================================="
echo
echo "SHA-256:"
echo "$ACTUAL_SHA"
echo
echo "Architecture:"
echo "ARM64 / AArch64"
echo
echo "GNU Build ID:"
echo "$ACTUAL_BUILD"
echo
echo "All checks passed."
echo "=========================================="
