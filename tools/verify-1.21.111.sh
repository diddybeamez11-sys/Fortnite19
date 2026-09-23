#!/usr/bin/env bash
set -euo pipefail
APK="${1:?Usage: verify-1.21.111.sh minecraft-1.21.111.apk}"
EXPECTED_SHA="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"
EXPECTED_BUILD="ea5614dcc3551721b14f9b4686aa1651c1b8f79e"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
sha=$(sha256sum "$APK" | awk '{print $1}')
[[ "$sha" == "$EXPECTED_SHA" ]] || { echo "APK SHA mismatch: $sha"; exit 3; }
unzip -p "$APK" lib/arm64-v8a/libminecraftpe.so > "$TMP/libminecraftpe.so"
python3 - "$TMP/libminecraftpe.so" "$EXPECTED_BUILD" <<'PY'
import struct,sys
p,expected=sys.argv[1:]; d=open(p,'rb').read(); e='<'
phoff=struct.unpack_from('<Q',d,32)[0]; entsz,phnum=struct.unpack_from('<HH',d,54)
found=''
for i in range(phnum):
 o=phoff+i*entsz; typ=struct.unpack_from('<I',d,o)[0]
 if typ!=4: continue
 off,sz=struct.unpack_from('<QQ',d,o+8+16); x=d[off:off+sz]; pos=0
 while pos+12<=len(x):
  ns,ds,nt=struct.unpack_from('<III',x,pos); pos+=12
  name=x[pos:pos+ns].rstrip(b'\0'); pos+=(ns+3)//4*4
  desc=x[pos:pos+ds]; pos+=(ds+3)//4*4
  if name==b'GNU' and nt==3: found=desc.hex()
print('libminecraftpe.so GNU build ID:',found)
if found != expected: raise SystemExit('GNU build ID mismatch')
PY
echo "Minecraft 1.21.111 ARM64 input verified."
