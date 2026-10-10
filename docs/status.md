# Status

openbv is early. The game runs and plays locally and online, on the openbv server.

## Works

- Start-up: the RndLabs intro and every main menu page (News, Profile, Game Browser, Host, Map Editor,
  Options, Credits), in openbv's redesigned look: 16:9, flat panels, one accent colour, the Rubik font.
  The game renders at the window's 16:9 size (1920x1080 by default); gameplay is the original's.
- Settings: `bv2.cfg` and `bv2.db` changes saved in gasm storage.
- Hosting a game: the server in the same process, the client joining it (tried with free for all
  on CTF-Daivuk; the other game types and the map rotation not yet).
- Playing on a hosted game: spawning, moving, weapons, projectiles, particles, the minimap, the
  console.
- The HUD: the original's, drawn straight over the game, except a slim health gauge with the number
  (green to red) and a scoreboard like the menus, with team bands, aligned columns and the local
  player marked. It shows what the original showed.
- Sound: effects (2D and 3D) and music.
- Keyboard and mouse as DirectInput read them; the first gamepad's sticks and triggers.
- The browser: [/play/](/play/) runs the same module on WebGL 2, with the data from the site (cached
  after the first visit) and the settings in the browser's storage.
- Online play on [the openbv server](/guide/server) (Go): several sessions per process, every game
  type, the original's rules ported and checked against its code, votes, in-game admins, bans, an admin
  page, and the Game Browser listing the server's games (`--master=<host>:<port>`).
- Determinism: identical video and audio hashes on gasm's native and Node runners and in the
  browser player (`tools/web-play-test.mjs`: the menu and a hosted game, played).

## Doesn't work yet

- **Hosting from the game for others.** A game hosted in the client is reachable by that client only
  (gasm guests can't listen); others play on [the openbv server](/guide/server).
- **LAN search, server pings, remote admin.** They used baboNet's peer-to-peer UDP, which has no
  transport on gasm.
- **Gamepad buttons.** Only the sticks and triggers are mapped.
- **The Prozac mod's data** runs with openbv's font (Prozac ships `fonts/font.tga` where the game
  loads `fonts/babo.tga`; openbv's own font covers both); the rest of Prozac's changes to the game
  aren't ported.
- **The account server** (profiles, friends, clans, stats) is gone; those requests fail as an
  unreachable server did.
