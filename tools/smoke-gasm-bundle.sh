#!/usr/bin/env bash
# Check a gasm bundle from tools/package-gasm.sh (CI runs it on each bundle's own OS):
#   tools/smoke-gasm-bundle.sh <openbv-gasm-...-macos-universal.zip | ...-linux-<arch>.tar.gz>
# Unpacks it, checks the files (the module, the content, ANGLE, the licenses), the launcher's --help and
# --dry-run, then plays 600 frames headless through the launcher (a scratch HOME, no window, no sound) and
# compares the hash line with tools/headless-check.sh's expected one: the bundled gasm-run, module and
# content give the same game as a direct run.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCHIVE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
case "$ARCHIVE" in
  *.zip) (cd "$T" && unzip -q "$ARCHIVE") ;;
  *.tar.gz) tar xzf "$ARCHIVE" -C "$T" ;;
  *) fail "unknown archive $ARCHIVE" ;;
esac
DIR=$(find "$T" -mindepth 1 -maxdepth 1 -type d -name 'openbv-gasm-*' | head -n 1)
[ -n "$DIR" ] || fail "no openbv-gasm-* folder in the archive"
for f in README.txt LICENSE LICENSE-gasm THIRD-PARTY; do [ -s "$DIR/$f" ] || fail "missing $f"; done
if [ -d "$DIR/OpenBV.app" ]; then
  APP="$DIR/OpenBV.app"
  RUN="$APP/Contents/MacOS/gasm-run"; WASM="$APP/Contents/Resources/openbv.wasm"; L="$APP/Contents/MacOS/OpenBV"
  DATA="$APP/Contents/Resources/data"; GL="$APP/Contents/Frameworks"
  codesign --verify --strict "$APP" || fail "code signature"
  [ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")" = pl.emdzej.openbv.gasm ] ||
    fail "bundle id"
  lipo -info "$RUN"
  [ -f "$GL/libEGL.dylib" ] && [ -f "$GL/libGLESv2.dylib" ] || fail "ANGLE missing from Frameworks"
else
  RUN="$DIR/gasm-run"; WASM="$DIR/openbv.wasm"; L="$DIR/openbv.sh"; DATA="$DIR/data"
  [ -f "$DIR/libEGL.so" ] && [ -f "$DIR/libGLESv2.so" ] || fail "ANGLE missing"
  [ -s "$DIR/openbv.png" ] || fail "missing openbv.png"
fi
if [ ! -x "$RUN" ] || [ ! -s "$WASM" ] || [ ! -x "$L" ]; then fail "gasm-run, openbv.wasm or the launcher missing"; fi
[ -s "$DATA/bv2.db" ] && [ -d "$DATA/main/maps" ] && [ -s "$DATA/main/License.txt" ] || fail "content incomplete"
[ "$(find "$DATA" -type f | wc -l | tr -d ' ')" = 328 ] || fail "content: $(find "$DATA" -type f | wc -l) files, expected 328"
grep -q "gasm-run [0-9]" "$DIR/README.txt" || fail "README does not name the gasm version"

# Launcher: help, and the command it builds (scratch HOME so nothing real is touched).
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state" XDG_DATA_HOME="$T/home/.local/share"
mkdir -p "$HOME"
"$L" --help | grep -q "OpenBV" || fail "--help"
"$L" --dry-run --param master=example.org:3333 | tee "$T/cmd"
{ grep -q -- "--asset-dir" "$T/cmd" && grep -q "master=example.org:3333" "$T/cmd" && grep -q -- "--storage-id openbv" "$T/cmd"; } ||
  fail "dry run"

# The game: 600 headless frames through the launcher, the expected hashes.
OPENBV_NO_LOG=1 "$L" --headless 600 --mute 2>&1 | tee "$T/out" | grep fnv || true
expected=$("$ROOT/tools/headless-check.sh" --expected)
grep -qx "$expected" "$T/out" || fail "headless run: expected '$expected'"
echo "PASS $(basename "$ARCHIVE")"
