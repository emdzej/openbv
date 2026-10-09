#!/usr/bin/env bash
# Run openbv.wasm headless for 600 frames (the RndLabs intro into the main menu) with the vanilla 2.11
# content and compare the hash line with the expected one. Native gasm-run and the Node runner print the
# same; a change that alters it must be explained by the change (and update EXPECTED here).
#   tools/headless-check.sh <gasm-run> <openbv.wasm> <content dir>   -> exit 0 if equal
#   tools/headless-check.sh --expected                               -> prints the expected hash line
set -euo pipefail
FRAMES=600
EXPECTED="video_fnv32=1be66dd1 audio_fnv32=b56bc7ab audio_frames=220500"
if [ "${1:-}" = --expected ]; then echo "$EXPECTED"; exit 0; fi
RUN=${1:?usage: headless-check.sh <gasm-run> <openbv.wasm> <content dir>}; WASM=${2:?}; DATA=${3:?}
line=$("$RUN" "$WASM" --asset-dir "$DATA" --headless "$FRAMES" --mute 2>&1 | grep -E '^video_fnv32=' || true)
echo "$line"
if [ "$line" != "$EXPECTED" ]; then
  echo "headless-check: expected $EXPECTED" >&2
  exit 1
fi
echo "headless-check: OK ($FRAMES frames)"
