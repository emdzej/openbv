---
layout: home

hero:
  name: openbv
  text: Babo Violent 2 on gasm
  tagline: The original game, built from its GPL source, running on macOS, Linux and Windows from one WebAssembly file. Same rules, same maps, same babos.
  actions:
    - theme: brand
      text: Download
      link: https://github.com/emdzej/openbv/releases
    - theme: alt
      text: Get started
      link: /guide/
    - theme: alt
      text: Source
      link: https://github.com/emdzej/openbv

features:
  - title: The original game
    details: Babo Violent 2 2.11, the last version RndLabs shipped, ported from the source Daivuk released under the GPL. The game code is the original code, not a remake of it.
  - title: Runs where gasm runs
    details: One openbv.wasm, on gasm's native runner for macOS, Linux and Windows. The browser is next; gasm already runs there, the game needs a way to get its data.
  - title: Online play (in progress)
    details: A server written in Go that hosts many game sessions at once, with an admin page for players, bans, map rotation and settings. Not there yet; hosting a game on your own machine works today.
  - title: Faithful
    details: The same rules, the same physics, the same assets. Where the port has to replace something (OpenGL 1, FMOD, sockets), the replacement follows the original's behaviour, quirks included.
---

## Why

Babo Violent 2 was one of those small games that ate whole evenings: little balls with guns, a top-down
arena, capture the flag, and a community that kept it alive long after its developers moved on. Then
the servers went dark one by one, the master server with them, and what's left is an old Windows
build that wants DirectInput and FMOD 3.

What does it take to get it back? Less than you'd think — the source is public. Daivuk released it
under the GPL, so this isn't a reverse-engineering project like
[OpenRF](https://openrf.emdzej.pl) or [OpenGTA](https://opengta.emdzej.pl). It's a port: the game code
stays as it was, and everything underneath it — the window, OpenGL 1.x, FMOD, the network library —
is rebuilt on [gasm](https://gasm.emdzej.pl), so the same file runs everywhere gasm does.

## Screenshots

<div class="shots">
  <figure><img src="/screenshots/menu.webp" alt="The main menu" loading="lazy"><figcaption>The 2.11 main menu, as the original draws it.</figcaption></figure>
  <figure><img src="/screenshots/host.webp" alt="Hosting a game" loading="lazy"><figcaption>Hosting a game: the server runs in the same process, as it did on Windows.</figcaption></figure>
  <figure><img src="/screenshots/team-select.webp" alt="Choosing a weapon on CTF-Daivuk" loading="lazy"><figcaption>Choosing a weapon on CTF-Daivuk.</figcaption></figure>
  <figure><img src="/screenshots/in-game.webp" alt="A babo firing the SMG" loading="lazy"><figcaption>In the game: the SMG, its tracers, smoke and shells.</figcaption></figure>
</div>

The background up there isn't a video either: it's a 30 KB gasm game of its own, two babos settling
their differences, running in your browser on gasm's web host.

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>
