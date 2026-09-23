#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Arguments:
#   $1 = original Minecraft APK
#   $2 = compiled eclient_host.so
#   $3 = final patched APK

INPUT_APK="${1:-}"
HOST_SO="${2:-}"
OUTPUT_APK="${3:-$ROOT/E-Client-Minecraft-1.21.111-ARM64.apk}"

WORK="$ROOT/.patch-work"

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

PROVIDER="com.rubidiumclient.host.EClientLoaderProvider"
AUTHORITY="com.rubidiumclient.eclientloader"

# IMPORTANT:
# The loader is located in tools/, not native/runtime/src/host/.
HOST_SMALI="$ROOT/tools/minecraft-host-loader.smali"

if [[ -z "$INPUT_APK" || -z "$HOST_SO" ]]; then
    echo "Usage:"
    echo "$0 <input.apk> <host.so> [output.apk]"
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: Input APK does not exist:"
    echo "$INPUT_APK"
    exit 1
fi

if [[ ! -f "$HOST_SO" ]]; then
    echo "ERROR: Native host library does not exist:"
    echo "$HOST_SO"
    exit 1
fi

if [[ ! -f "$HOST_SMALI" ]]; then
    echo "ERROR: Loader source does not exist:"
    echo "$HOST_SMALI"
    exit 1
fi

echo "=========================================="
echo " E-CLIENT APK PATCHER"
echo " Minecraft Bedrock 1.21.111 ARM64"
echo "=========================================="

echo
echo "[1/11] Checking required tools..."

for tool in \
    apktool \
    zipalign \
    apksigner \
    aapt2 \
    python3 \
    sha256sum \
    keytool \
    file
do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: Missing required tool: $tool"
        exit 1
    fi
done

echo "Required tools available."

echo
echo "[2/11] Checking original Minecraft APK SHA-256..."

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected:"
echo "$EXPECTED_SHA256"

echo
echo "Actual:"
echo "$ACTUAL_SHA256"

if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
    echo
    echo "ERROR: Wrong Minecraft APK."
    exit 1
fi

echo
echo "Minecraft APK SHA-256 verified."

echo
echo "[3/11] Checking E-Client native library..."

file "$HOST_SO"

if ! file "$HOST_SO" | grep -Eiq \
    'ARM aarch64|ARM64|aarch64'
then
    echo
    echo "ERROR: eclient_host.so is not ARM64."
    exit 1
fi

echo
echo "ARM64 native library verified."

echo
echo "Loader:"
echo "$HOST_SMALI"

echo
echo "Native host:"
echo "$HOST_SO"

echo
echo "[4/11] Preparing workspace..."

rm -rf "$WORK"
mkdir -p "$WORK"

DECODED="$WORK/mc"
UNSIGNED="$WORK/rebuilt-unsigned.apk"
ALIGNED="$WORK/aligned.apk"
KEYSTORE="$WORK/eclient-debug.keystore"

echo
echo "[5/11] Decoding Minecraft APK..."

apktool d \
    --force \
    "$INPUT_APK" \
    -o "$DECODED"

if [[ ! -f "$DECODED/AndroidManifest.xml" ]]; then
    echo "ERROR: AndroidManifest.xml missing after decode."
    exit 1
fi

if [[ ! -f "$DECODED/apktool.yml" ]]; then
    echo "ERROR: apktool.yml missing after decode."
    exit 1
fi

echo
echo "[6/11] Configuring APK resources..."

python3 - "$DECODED/apktool.yml" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])

text = path.read_text(encoding="utf-8")

lines = text.splitlines()

result = []
found = False

for line in lines:
    if line.strip().startswith("resourcesAreCompressed:"):
        result.append("resourcesAreCompressed: false")
        found = True
    else:
        result.append(line)

if not found:
    result.append("resourcesAreCompressed: false")

path.write_text(
    "\n".join(result) + "\n",
    encoding="utf-8"
)

print("resourcesAreCompressed: false")
PY

echo
echo "Installing E-Client loader..."

SMALI_DIR="$DECODED/smali/com/rubidiumclient/host"

mkdir -p "$SMALI_DIR"

cp \
    "$HOST_SMALI" \
    "$SMALI_DIR/EClientLoaderProvider.smali"

if [[ ! -f "$SMALI_DIR/EClientLoaderProvider.smali" ]]; then
    echo "ERROR: Loader installation failed."
    exit 1
fi

echo
echo "Installing ARM64 native library..."

LIB_DIR="$DECODED/lib/arm64-v8a"

mkdir -p "$LIB_DIR"

cp \
    "$HOST_SO" \
    "$LIB_DIR/libeclient_host.so"

if [[ ! -f "$LIB_DIR/libeclient_host.so" ]]; then
    echo "ERROR: Native library installation failed."
    exit 1
fi

echo
echo "Updating AndroidManifest.xml..."

python3 \
    "$DECODED/AndroidManifest.xml" \
    "$PROVIDER" \
    "$AUTHORITY" <<'PY'
import sys
import re
from pathlib import Path

manifest_path = Path(sys.argv[1])
provider = sys.argv[2]
authority = sys.argv[3]

text = manifest_path.read_text(encoding="utf-8")

if provider in text:
    print("Provider already exists.")
    sys.exit(0)

provider_xml = (
    f'<provider '
    f'android:name="{provider}" '
    f'android:authorities="{authority}" '
    f'android:exported="false" '
    f'android:initOrder="1000" />'
)

match = re.search(r"</application\s*>", text)

if not match:
    print("ERROR: Could not find </application>.")
    sys.exit(1)

text = (
    text[:match.start()]
    + provider_xml
    + "\n"
    + text[match.start():]
)

manifest_path.write_text(
    text,
    encoding="utf-8"
)

print("Provider added.")
PY

if ! grep -q "$PROVIDER" "$DECODED/AndroidManifest.xml"; then
    echo "ERROR: Provider was not added."
    exit 1
fi

echo
echo "[7/11] Rebuilding APK..."

rm -f "$UNSIGNED"

apktool b \
    "$DECODED" \
    -o "$UNSIGNED"

if [[ ! -s "$UNSIGNED" ]]; then
    echo "ERROR: Apktool rebuild failed."
    exit 1
fi

echo
echo "[8/11] Verifying resources.arsc..."

python3 - "$UNSIGNED" <<'PY'
import sys
import zipfile

apk = sys.argv[1]

with zipfile.ZipFile(apk, "r") as z:

    if "resources.arsc" not in z.namelist():
        print("ERROR: resources.arsc missing.")
        sys.exit(1)

    info = z.getinfo("resources.arsc")

    print(
        "resources.arsc compression method:",
        info.compress_type
    )

    if info.compress_type != zipfile.ZIP_STORED:
        print("ERROR: resources.arsc is compressed.")
        sys.exit(1)

print("resources.arsc is uncompressed.")
PY

echo
echo "[9/11] ZIP-aligning APK..."

rm -f "$ALIGNED"

zipalign \
    -P 16 \
    -f \
    4 \
    "$UNSIGNED" \
    "$ALIGNED"

zipalign \
    -c \
    -P 16 \
    -v \
    4 \
    "$ALIGNED"

echo
echo "[10/11] Creating signing key..."

keytool \
    -genkeypair \
    -v \
    -keystore "$KEYSTORE" \
    -storepass android \
    -alias androiddebugkey \
    -keypass android \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US" \
    >/dev/null 2>&1

mkdir -p "$(dirname "$OUTPUT_APK")"

rm -f "$OUTPUT_APK"

echo
echo "Signing APK..."

apksigner sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --ks-key-alias androiddebugkey \
    --v1-signing-enabled true \
    --v2-signing-enabled true \
    --v3-signing-enabled true \
    --out "$OUTPUT_APK" \
    "$ALIGNED"

if [[ ! -s "$OUTPUT_APK" ]]; then
    echo "ERROR: APK signing failed."
    exit 1
fi

echo
echo "[11/11] Final APK verification..."

echo
echo "===== SIGNATURE ====="

apksigner verify \
    --verbose \
    "$OUTPUT_APK"

echo
echo "===== ZIP ALIGNMENT ====="

zipalign \
    -c \
    -P 16 \
    -v \
    4 \
    "$OUTPUT_APK"

echo
echo "===== PACKAGE INFO ====="

aapt2 dump badging \
    "$OUTPUT_APK"

echo
echo "===== REQUIRED FILES ====="

python3 - "$OUTPUT_APK" <<'PY'
import sys
import zipfile

apk = sys.argv[1]

required = [
    "AndroidManifest.xml",
    "resources.arsc",
    "lib/arm64-v8a/libminecraftpe.so",
    "lib/arm64-v8a/libeclient_host.so",
]

with zipfile.ZipFile(apk, "r") as z:

    names = set(z.namelist())

    for item in required:

        if item not in names:
            print("ERROR: Missing:", item)
            sys.exit(1)

        print("OK:", item)

    resources = z.getinfo("resources.arsc")

    if resources.compress_type != zipfile.ZIP_STORED:
        print("ERROR: resources.arsc is compressed.")
        sys.exit(1)

print()
print("All required APK files verified.")
PY

echo
echo "=========================================="
echo " BUILD SUCCESSFUL"
echo "=========================================="

echo
echo "APK:"
echo "$OUTPUT_APK"

echo
echo "Size:"
ls -lh "$OUTPUT_APK"

echo
echo "SHA-256:"
sha256sum "$OUTPUT_APK"
