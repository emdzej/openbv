# Credits and licences

**openbv** is released under the [GNU General Public License v3.0](https://github.com/emdzej/openbv/blob/main/LICENSE)
(or later), the licence of the source it is built from.

**Babo Violent 2** was made by RndLabs. Its source, © 2012 bitHeads inc., was released under the
GPL-3.0 by David St-Louis (Daivuk): [github.com/Daivuk/BaboViolent2](https://github.com/Daivuk/BaboViolent2).
openbv's `src/game` and `src/engine` are that code.

**The game's data** (maps, models, textures, sounds, music) is RndLabs' work. It ships with the
original game and with its source repository; it is not covered by openbv's licence.

**Third-party code in openbv:**

- [stb_vorbis](https://github.com/nothings/stb) by Sean Barrett: public domain (or MIT), for the
  Ogg Vorbis music.
- [TinyXML](https://sourceforge.net/projects/tinyxml/) and the MD5 implementations, as included in
  the original source.

**Runtime:** [gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C/C++ SDK, and on this site
`@emdzej/gasm-host`, which runs the home page's background.

The home page's background (two babos shooting each other) is openbv's own: drawn procedurally by
`tools/site-bg/babos.c`, no game data in it.
