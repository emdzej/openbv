# Status

openbv is early. The game runs and plays locally; online play doesn't exist yet.

## Works

- Start-up: the RndLabs intro, the 2.11 main menu, the Host page, fonts, textures, tooltips. The
  other menu pages haven't been gone through yet.
- Settings: `bv2.cfg` and `bv2.db` changes saved in gasm storage.
- Hosting a game: the server in the same process, the client joining it (tried with free for all
  on CTF-Daivuk; the other game types and the map rotation not yet).
- Playing on a hosted game: spawning, moving, weapons, projectiles, particles, the HUD, the minimap,
  the console.
- Sound: effects (2D and 3D) and music are mixed; in a window, gun sounds have been reported
  missing, which is being looked into.
- Keyboard and mouse as DirectInput read them; the first gamepad's sticks and triggers.
- Determinism: identical video and audio hashes on gasm's native and Node runners.

## Doesn't work yet

- **Online play.** Hosted games are reachable by the game itself only (gasm guests can't listen).
  The openbv server (Go, many sessions, an admin page) is in progress.
- **The Game Browser.** It needs a master server; the original one is gone, and the openbv server
  will take its place.
- **LAN search, server pings, remote admin.** They used baboNet's peer-to-peer UDP, which has no
  transport on gasm.
- **The browser.** gasm runs there; the game needs a way to load its data.
- **Gamepad buttons.** Only the sticks and triggers are mapped.
- **The Prozac mod's data.** Its menus draw without text: its font is a different, larger texture,
  which the Prozac build handled differently. Prozac support is planned as an option next to the
  vanilla data.
- **The account server** (profiles, friends, clans, stats) is gone; those requests fail as an
  unreachable server did.
