#!/usr/bin/env bash
# Screenshots of the port for the site: gasm-run headless renders of build-gasm/openbv.wasm with the
# vanilla data (ref/, see ./play), written to docs/public/screenshots/. Never screen captures.
#   docs/scripts/screenshots.sh            (RUN=<gasm-run>, DATA=<game folder> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
RUN=${RUN:-../gasm/runners/native/target/release/gasm-run}
DATA=${DATA:-ref/BaboViolent2/BaboViolent2/Content}
OUT=docs/public/screenshots
[[ -f build-gasm/openbv.wasm ]] || { echo "screenshots: build the game first (./play builds it)" >&2; exit 1; }
[[ -f "$DATA/bv2.db" ]] || { echo "screenshots: no game data in $DATA (./play fetches it)" >&2; exit 1; }
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# the clicks: Host tab, Start, a weapon (the SMG), Auto assign team, shoot to spawn, then fire
HOST="610:PTR(630,54),620:PTR(630,54,L),625:PTR(630,54)"
START="700:PTR(266,101),720:PTR(266,101,L),740:PTR(266,101)"
PLAY="$HOST,$START,1000:PTR(579,174),1010:PTR(579,174,L),1020:PTR(579,174),1060:PTR(309,174),1070:PTR(309,174,L),1080:PTR(309,174),1300:PTR(800,300),1310:PTR(800,300,L),1320:PTR(800,300),1600-1640:PTR(900,250,L)"

shot() {   # name frames [input]
  local args=("$RUN" build-gasm/openbv.wasm --asset-dir "$DATA" --headless "$2" --screenshot "$tmp/$1.png")
  [[ -n "${3:-}" ]] && args+=(--input "$3")
  "${args[@]}" >/dev/null 2>&1
  # the frame is 4:3 in the middle of the 1280x720 drawable: crop the black bars
  if command -v magick >/dev/null; then magick "$tmp/$1.png" -crop 960x720+160+0 +repage "$tmp/$1.png"
  elif command -v sips >/dev/null; then sips -c 720 960 "$tmp/$1.png" >/dev/null; fi
  if command -v cwebp >/dev/null; then cwebp -quiet -q 85 "$tmp/$1.png" -o "$OUT/$1.webp"; echo "$OUT/$1.webp"
  else cp "$tmp/$1.png" "$OUT/$1.png"; echo "$OUT/$1.png"; fi
}

shot menu 600
shot host 700 "$HOST"
shot team-select 1100 "$HOST,$START"
shot in-game 1640 "$PLAY"
