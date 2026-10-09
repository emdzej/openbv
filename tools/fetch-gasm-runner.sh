#!/usr/bin/env bash
# Download a released gasm runner (gasm-run) for one platform, for tools/package-gasm.sh and CI.
#   tools/fetch-gasm-runner.sh <platform> [dest dir]
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. The version is GASM_VERSION
# from tools/fetch-gasm-sdk.sh (the single pin). openbv draws with gasm:gl, so next to gasm-run[.exe] this
# keeps what the release ships for it: ANGLE (libEGL, libGLESv2), SwiftShader and the Vulkan loader (GPU-less
# fallback, --gl-software) and ANGLE-NOTICES.txt, plus gasm's LICENSE (MIT; the archives don't carry it).
# Puts them in <dest dir> (default .deps/gasm-runner-<platform>) and prints the directory.
set -euo pipefail
cd "$(dirname "$0")/.."
PLATFORM=${1:?usage: fetch-gasm-runner.sh <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> [dest dir]}
V=$(tools/fetch-gasm-sdk.sh --version)
DEST=${2:-.deps/gasm-runner-$PLATFORM}
case "$PLATFORM" in
  windows-*) ARCHIVE="gasm-$V-$PLATFORM.zip"; EXE=.exe ;;
  macos-universal|linux-x86_64|linux-arm64) ARCHIVE="gasm-$V-$PLATFORM.tar.gz"; EXE= ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 2 ;;
esac
if [ -x "$DEST/gasm-run$EXE" ] && [ -f "$DEST/LICENSE" ] && [ "$(cat "$DEST/VERSION" 2>/dev/null)" = "$V" ]; then
  echo "$DEST"; exit 0
fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
base=https://github.com/emdzej/gasm/releases/download/$V
echo "fetching $base/$ARCHIVE" >&2
curl -fsSL -o "$TMP/$ARCHIVE" "$base/$ARCHIVE"
case "$ARCHIVE" in
  *.zip) (cd "$TMP" && unzip -q "$ARCHIVE") ;;
  *) tar xzf "$TMP/$ARCHIVE" -C "$TMP" ;;
esac
SRC="$TMP/gasm-$V-$PLATFORM"
rm -rf "$DEST"; mkdir -p "$DEST"
cp "$SRC/gasm-run$EXE" "$DEST/"
for f in "$SRC"/libEGL.* "$SRC"/libGLESv2.* "$SRC"/libvk_swiftshader.* "$SRC"/vk_swiftshader.dll "$SRC"/libvulkan.* \
         "$SRC"/vulkan-1.dll "$SRC"/vk_swiftshader_icd.json "$SRC"/ANGLE-NOTICES.txt; do
  [ -e "$f" ] && cp "$f" "$DEST/"
done
[ -e "$DEST/ANGLE-NOTICES.txt" ] || { echo "fetch-gasm-runner: no ANGLE in $ARCHIVE" >&2; exit 1; }
curl -fsSL -o "$DEST/LICENSE" "https://raw.githubusercontent.com/emdzej/gasm/$V/LICENSE"
echo "$V" > "$DEST/VERSION"
chmod +x "$DEST/gasm-run$EXE"
echo "$DEST"
