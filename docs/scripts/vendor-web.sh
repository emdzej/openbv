#!/usr/bin/env bash
# Copy gasm's browser runtime from the pinned npm package (@emdzej/gasm-host, MIT) into
# docs/public/play/: the home page's background and the player (/play/) load /play/gasm-host.js, which
# imports ./lib/.
# Run after `pnpm install` in docs/; the output is git-ignored.
set -euo pipefail
cd "$(dirname "$0")/.."
src=node_modules/@emdzej/gasm-host
out=public/play
mkdir -p "$out"
rm -rf "$out/lib"
cp "$src/gasm-host.js" "$src/input-script.mjs" "$src/LICENSE" "$out/"   # input-script: the player's hash runs
cp -R "$src/lib" "$out/lib"
node -e "const p=require('./$src/package.json');console.log(p.name+'@'+p.version+' ('+p.license+')')" > "$out/VERSION"
echo "vendored $(cat "$out/VERSION")"
