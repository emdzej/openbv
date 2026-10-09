#!/usr/bin/env bash
# The browser player's game data: tools/fetch-content.sh (Babo Violent 2.11's bv2.db and main/, from
# the GPL source release at the pinned commit) copied to docs/public/play/data/ (git-ignored), with
# data/files.json: { version, files: [[name, size], ...] }, which the page reads instead of listing a
# directory. version is the content's tree hash: the page caches the files under it.
set -euo pipefail
cd "$(dirname "$0")/../.."
content=$(tools/fetch-content.sh)
out=docs/public/play/data
rm -rf "$out"; mkdir -p "$out"
(cd "$content" && find . -type f ! -name '.*' | sed 's|^\./||' | LC_ALL=C sort) > /tmp/openbv-files.$$
while read -r f; do mkdir -p "$out/$(dirname "$f")"; cp "$content/$f" "$out/$f"; done < /tmp/openbv-files.$$
version=$(tools/fetch-content.sh --hash "$content")
node -e '
const fs = require("fs"), path = require("path");
const [list, out, version] = process.argv.slice(1);
const files = fs.readFileSync(list, "utf8").split("\n").filter(Boolean).map((n) => [n, fs.statSync(path.join(out, n)).size]);
fs.writeFileSync(path.join(out, "files.json"), JSON.stringify({ version, files }));
console.log(`copied ${files.length} files, ${(files.reduce((s, f) => s + f[1], 0) / 1048576).toFixed(1)} MB, version ${version.slice(0, 12)}`);
' /tmp/openbv-files.$$ "$out" "$version"
rm -f /tmp/openbv-files.$$
