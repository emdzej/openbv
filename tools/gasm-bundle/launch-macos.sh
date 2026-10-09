#!/usr/bin/env bash
# OpenBV @VERSION@ on gasm-run @GASM_VERSION@: the macOS app's executable (Contents/MacOS/OpenBV).
# Runs the bundled openbv.wasm with the game's content from Contents/Resources/data. Options:
#   --help       this
#   --dry-run    print the gasm-run command instead of running it (also OPENBV_DRY_RUN=1)
# OPENBV_NO_LOG=1 keeps the output on stdout even without a terminal (tests).
# Anything else goes to gasm-run (e.g. --window 1280x960, --filter fsr, --param master=host:port).
# Started from Finder, the output goes to ~/Library/Logs/OpenBV/gasm.log.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$HERE/../Resources"
DRY=${OPENBV_DRY_RUN:-0}
args=()
for a in "$@"; do
  case "$a" in
    --help|-h)
      sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --dry-run) DRY=1 ;;
    -psn_*) ;;   # Finder's process serial number on old systems
    *) args+=("$a") ;;
  esac
done
# shellcheck disable=SC2054  # the comma is gasm-run's host list
cmd=("$HERE/gasm-run" "$RES/openbv.wasm" --asset-dir "$RES/data" --storage-id openbv
  --allow-net=127.0.0.1,localhost --window 1024x768 ${args[@]+"${args[@]}"})
if [ "$DRY" = 1 ]; then printf '%q ' "${cmd[@]}"; echo; exit 0; fi
if [ ! -t 1 ] && [ "${OPENBV_NO_LOG:-0}" != 1 ]; then
  LOG="$HOME/Library/Logs/OpenBV"; mkdir -p "$LOG"
  exec "${cmd[@]}" >"$LOG/gasm.log" 2>&1
fi
exec "${cmd[@]}"
