#!/usr/bin/env bash
# Package openbv.wasm, the game's content and a released gasm-run into a double-clickable bundle.
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe],
# ANGLE and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENBV_WASM=<file> picks the
# module (default build-gasm/openbv.wasm), OPENBV_CONTENT=<dir> the content (default: tools/fetch-content.sh,
# .deps/content: bv2.db and main/). Produces in <out dir>:
#   macos-universal  OpenBV.app, openbv-gasm-<version>-macos-universal.zip
#   linux-<arch>     openbv-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   openbv-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/. No AOT .cwasm: gasm-run --compile only targets the host it runs on.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENBV_WASM:-$ROOT/build-gasm/openbv.wasm}
CONTENT=${OPENBV_CONTENT:-}
[ -n "$CONTENT" ] || CONTENT="$ROOT/$(cd "$ROOT" && tools/fetch-content.sh)"
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$RUNDIR/ANGLE-NOTICES.txt" "$CONTENT/bv2.db"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM, tools/fetch-content.sh)" >&2; exit 1; }
done
[ -d "$CONTENT/main" ] || { echo "missing $CONTENT/main" >&2; exit 1; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="openbv-gasm-$VERSION-$PLATFORM"

fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" > "$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" > "$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi > "$1.sha256"); }
# the content: bv2.db and main/ (hidden files and the fetch marker left out)
copy_content() { mkdir -p "$1"; cp "$CONTENT/bv2.db" "$1/"; cp -R "$CONTENT/main" "$1/"; find "$1" -name '.*' -type f -delete; }
# the ANGLE / SwiftShader / Vulkan libraries next to gasm-run
gl_libs() { for f in "$RUNDIR"/lib* "$RUNDIR"/*.dll "$RUNDIR"/*.json; do [ -e "$f" ] && cp "$f" "$1/"; done; return 0; }

# notices <out file>: what is in the bundle and under which terms
notices() {
  cat > "$1" <<TXT
OpenBV $VERSION, third-party components and content

openbv.wasm         OpenBV, GPL-3.0-or-later (LICENSE). It is Babo Violent 2's source code as RndLabs
                    released it under the GPL-3.0 (https://github.com/Daivuk/BaboViolent2), with
                    OpenBV's platform layer for gasm.
  stb_vorbis        Ogg Vorbis decoder by Sean Barrett, public domain (or MIT), in openbv.wasm.
gasm-run            gasm $GASM_VERSION, MIT (LICENSE-gasm), https://github.com/emdzej/gasm
ANGLE, SwiftShader  OpenGL ES for gasm-run, BSD-style licenses (ANGLE-NOTICES.txt)
data/               Babo Violent 2's content (maps, models, textures, sounds, fonts, languages) and
                    bv2.db, (c) RndLabs, taken unchanged from the same source release
                    (BaboViolent2/Content). Its license as shipped there: data/main/License.txt.
TXT
}

# readme <out file>
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open OpenBV.app. It is signed ad hoc, not notarized: the first time, right-click it and
choose Open (or: xattr -dr com.apple.quarantine OpenBV.app).
From Terminal: OpenBV.app/Contents/MacOS/OpenBV --help'
      saves='~/Library/Application Support/gasm/openbv/'
      logs='~/Library/Logs/OpenBV/gasm.log (started from Finder)' ;;
    linux-*)
      start='Run ./openbv.sh (from a terminal or your file manager). ./openbv.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and
Ubuntu) and a Vulkan or OpenGL capable graphics driver. ./openbv.sh --help lists the options.'
      saves='~/.local/share/gasm/openbv/'
      logs='the terminal, or ~/.local/state/openbv/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenBV.cmd. (If Windows SmartScreen warns about gasm-run.exe: More info,
Run anyway.) OpenBV.cmd --help lists the options.'
      saves='%APPDATA%\gasm\openbv\'
      logs='the console window' ;;
  esac
  cat > "$1" <<TXT
OpenBV $VERSION for gasm ($PLATFORM)
https://openbv.emdzej.pl

OpenBV is Babo Violent 2 (RndLabs, 2.11) on gasm: the game's own GPL-3.0 source, built as a
WebAssembly module (openbv.wasm) and run by gasm-run $GASM_VERSION (https://gasm.emdzej.pl). The
game's content is included (data/).

START
$start

CONTROLS
The original game's (Options changes them): W A S D move, the mouse aims, left button shoots,
right button throws a grenade, middle button a molotov, Space melee, F picks up, T / Y chat (all /
team), Tab the scores, Esc the menu. The backquote key opens the console. Holding Esc for a second
quits (gasm-run).

SETTINGS AND SAVES
$saves

LOG
$logs

LICENSES
OpenBV is GPL-3.0 (LICENSE$lic). gasm-run is MIT (LICENSE-gasm$lic). What else is inside and
under which terms: THIRD-PARTY$lic.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/OpenBV.app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  # ANGLE only: every Mac has Metal, and SwiftShader's ICD file (vk_swiftshader_icd.json, for
  # --gl-software) is no code, which the app's signature doesn't allow among the frameworks
  cp "$RUNDIR/libEGL.dylib" "$RUNDIR/libGLESv2.dylib" "$APP/Contents/Frameworks/"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenBV"
  chmod +x "$APP/Contents/MacOS/OpenBV" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/openbv.wasm"
  copy_content "$APP/Contents/Resources/data"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  cp "$RUNDIR/ANGLE-NOTICES.txt" "$APP/Contents/Resources/"
  notices "$APP/Contents/Resources/THIRD-PARTY"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/openbv.icns"
  cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>OpenBV</string>
  <key>CFBundleDisplayName</key><string>OpenBV</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.openbv.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenBV $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenBV</string>
  <key>CFBundleIconFile</key><string>openbv</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenBV, GPL-3.0; gasm-run MIT. Babo Violent 2 and its content are (c) RndLabs.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    for f in "$APP/Contents/Frameworks"/*.dylib "$APP/Contents/MacOS/gasm-run"; do codesign --force --sign - "$f"; done
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  notices "$STAGE/$NAME/THIRD-PARTY"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  gl_libs "$D"
  cp "$WASM" "$D/openbv.wasm"
  copy_content "$D/data"
  fill "$SRC/openbv.sh" "$D/openbv.sh"
  cp "$SRC/openbv-gasm.desktop" "$D/"
  python3 "$ROOT/tools/icon.py" 256 "$D/openbv.png"
  chmod 755 "$D/gasm-run" "$D/openbv.sh"; chmod 644 "$D/openbv.wasm"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"; cp "$RUNDIR/ANGLE-NOTICES.txt" "$D/"
  notices "$D/THIRD-PARTY"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  gl_libs "$D"
  cp "$WASM" "$D/openbv.wasm"
  copy_content "$D/data"
  fill "$SRC/OpenBV.cmd" "$D/OpenBV.cmd"; fill "$SRC/openbv.ps1" "$D/openbv.ps1"
  python3 "$ROOT/tools/icon.py" 256 "$D/openbv.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"; cp "$RUNDIR/ANGLE-NOTICES.txt" "$D/"
  notices "$D/THIRD-PARTY.txt"
  for f in OpenBV.cmd openbv.ps1 README.txt LICENSE.txt LICENSE-gasm.txt THIRD-PARTY.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
