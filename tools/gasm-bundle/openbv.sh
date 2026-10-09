#!/usr/bin/env bash
# OpenBV @VERSION@ on gasm-run @GASM_VERSION@ for Linux. Runs openbv.wasm with the game's content from
# data/ next to this script. Options:
#   --help              this
#   --dry-run           print the gasm-run command instead of running it (also OPENBV_DRY_RUN=1)
# OPENBV_NO_LOG=1 keeps the output on stdout even without a terminal (tests).
#   --install-desktop   add a menu entry (~/.local/share/applications/openbv-gasm.desktop) and exit
# Anything else goes to gasm-run (e.g. --window 1920x1080, --filter fsr, --param master=host:port).
# Started without a terminal, the output goes to ${XDG_STATE_HOME:-~/.local/state}/openbv/gasm.log.
# gasm-run needs ALSA (libasound2 / libasound2t64) and a Vulkan or OpenGL capable graphics driver.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DRY=${OPENBV_DRY_RUN:-0}
args=()
for a in "$@"; do
  case "$a" in
    --help|-h) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --dry-run) DRY=1 ;;
    --install-desktop)
      apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"; mkdir -p "$apps"
      sed "s|@DIR@|$HERE|g" "$HERE/openbv-gasm.desktop" > "$apps/openbv-gasm.desktop"
      echo "added $apps/openbv-gasm.desktop"; exit 0 ;;
    *) args+=("$a") ;;
  esac
done
# shellcheck disable=SC2054  # the comma is gasm-run's host list
cmd=("$HERE/gasm-run" "$HERE/openbv.wasm" --asset-dir "$HERE/data" --storage-id openbv
  --allow-net=127.0.0.1,localhost --window 1600x900 --icon "$HERE/openbv.png" --app-class openbv
  ${args[@]+"${args[@]}"})
if [ "$DRY" = 1 ]; then printf '%q ' "${cmd[@]}"; echo; exit 0; fi
if [ ! -t 1 ] && [ ! -t 2 ] && [ "${OPENBV_NO_LOG:-0}" != 1 ]; then
  LOG="${XDG_STATE_HOME:-$HOME/.local/state}/openbv"; mkdir -p "$LOG"
  exec "${cmd[@]}" >"$LOG/gasm.log" 2>&1
fi
exec "${cmd[@]}"
