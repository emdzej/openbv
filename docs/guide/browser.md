# Playing in the browser

[/play/](/play/) runs openbv in the browser: the same `openbv.wasm` as the desktop bundles, on
gasm's web host (`@emdzej/gasm-host`), drawing with WebGL 2.

## What you need

A browser with WebAssembly and WebGL 2: a recent Chrome, Edge, Firefox or Safari. A keyboard and a
mouse. About 20 MB of download the first time.

## How it works

The page downloads the module and the game's data (bv2.db and `main/`, 328 files, 18 MB: the content of
the GPL source release, as the bundles ship it) and keeps the files in the browser's Cache API, so a
later visit starts without downloading them again. Press **Play** (the browser wants a click before it
plays sound), then click the picture so it gets the keyboard.

The game runs on the page's main thread at its own 60 frames a second, with the 800x600 picture scaled
to the window and letterboxed, as on the desktop. **Fullscreen** fills the screen with it.

Your settings (`bv2.cfg`: key bindings, player name, everything in Options) and the game's `bv2.db`
changes are kept in the browser's IndexedDB, under the site, separately from the desktop's.

## Keys

The same as on the desktop ([Controls](./controls)). The page hands every key to the game, so Space,
Tab and the function keys do what the game wants, not what the browser would. A tap of **Esc** goes to
the game (its menu); **holding Esc** for a second stops the game. The right mouse button throws grenades:
the browser's context menu is turned off over the picture.

## What doesn't work yet

The same as on the desktop ([Status](/status)): no online play until the openbv server exists, so
host a game and play it there. The game can't reach other hosts from the page; with a server running on
your own machine, `?master=host:port` sets the master server, as `./play --master` does.

## Checking it

`tools/web-play-test.mjs` runs the page in headless Chrome: the main menu and a hosted game played to
the first shots must give the hash line `gasm-run --headless` gives for the same frames and input, and a
live run must start on WebGL 2 and keep its data in the cache:

```sh
docs/scripts/copy-wasm.sh && docs/scripts/copy-content.sh
(cd docs && pnpm install && scripts/vendor-web.sh && pnpm build)
node tools/web-play-test.mjs
```

The page takes `?hashframes=N` (and `&input=...`, gasm-run's `--input` syntax) for that: N frames on
virtual time with the null GL and storage in memory, then the hash line in the status bar and in
`globalThis.__openbvResult`.
