#!/usr/bin/env bash
# Download the game's content (Babo Violent 2.11: bv2.db and main/) into .deps/content, for the bundles,
# CI and the browser player. It comes from RndLabs' source release on GitHub (Daivuk/BaboViolent2, the
# commit src/ was taken from), BaboViolent2/Content: bv2.db and main/ only (the Windows DLLs and the
# README there are left out).
#   tools/fetch-content.sh [dest dir]     -> prints the directory (default .deps/content)
# The tree is checked against CONTENT_SHA256: the SHA-256 of the sorted "sha256  path" lines of every
# file (as `tools/fetch-content.sh --hash <dir>` prints). There are no names differing only in case at
# this commit, so the content unpacks the same on case-insensitive file systems; the game's names
# (main/Sounds/Hit.wav for main/sounds/hit.wav) are matched case-insensitively by gasm.
set -euo pipefail
cd "$(dirname "$0")/.."
UPSTREAM=Daivuk/BaboViolent2
COMMIT=7171c84d9b4d02bb74e846d5617fbb527bfba358
CONTENT_FILES=328
CONTENT_SHA256=777b18f5aaa8b2e6d959351b49922838c2556edf54c3358c3354b707815822af

tree_hash() {
  (cd "$1" && find . -type f ! -name '.*' | LC_ALL=C sort | while read -r f; do
    if command -v sha256sum >/dev/null; then sha256sum "$f"; else shasum -a 256 "$f"; fi
  done) | if command -v sha256sum >/dev/null; then sha256sum; else shasum -a 256; fi | cut -d' ' -f1
}

if [ "${1:-}" = --hash ]; then tree_hash "${2:?usage: fetch-content.sh --hash <dir>}"; exit 0; fi

DEST=${1:-.deps/content}
count() { find "$1" -type f ! -name '.*' | wc -l | tr -d ' '; }
if [ -f "$DEST/bv2.db" ] && [ -f "$DEST/.content-ok" ] && [ "$(cat "$DEST/.content-ok")" = "$COMMIT" ]; then
  echo "$DEST"; exit 0
fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
url=https://codeload.github.com/$UPSTREAM/tar.gz/$COMMIT
echo "fetching $url" >&2
curl -fsSL "$url" | tar xz -C "$TMP" "BaboViolent2-$COMMIT/BaboViolent2/Content/bv2.db" "BaboViolent2-$COMMIT/BaboViolent2/Content/main"
SRC="$TMP/BaboViolent2-$COMMIT/BaboViolent2/Content"
n=$(count "$SRC")
[ "$n" = "$CONTENT_FILES" ] || { echo "fetch-content: $n files, expected $CONTENT_FILES" >&2; exit 1; }
h=$(tree_hash "$SRC")
[ "$h" = "$CONTENT_SHA256" ] || { echo "fetch-content: content hash $h, expected $CONTENT_SHA256" >&2; exit 1; }
rm -rf "$DEST"; mkdir -p "$(dirname "$DEST")"
mv "$SRC" "$DEST"
echo "$COMMIT" > "$DEST/.content-ok"
echo "$DEST"
