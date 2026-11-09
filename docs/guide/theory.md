# How Babo Violent 2 works

A theory of operation of Babo Violent 2 (RndLabs, version 2.11): how the game is built, from the frame
loop to a capture of the flag. It comes from porting the game's own C++ source, which its authors released
under the GPL in 2012, and from rewriting its server in Go against that source. It is meant for anyone who
wants to learn how a small online top-down shooter of the 2000s is put together. The details, with file
and line references, are in the repository: the server specification
([`design/server.md`](https://github.com/emdzej/openbv/blob/main/design/server.md)), the
[Architecture](/internals/) and [Networking](/internals/networking) pages, and
[`server/README.md`](https://github.com/emdzej/openbv/blob/main/server/README.md).

Everything here describes the original's design as the source shows it. Where openbv changes something
(the transport, the menus' look), it says so.

## The shape of the program

Babo Violent 2 is a C++ program of two layers. Below is *DukZeven*, the authors' small engine, shipped as a
set of DLLs, one per concern. Above it is the game, one executable that holds the menus, the client, the
server, the map editor and the console. The source is commented mostly in French.

| Module | What it is for |
|---|---|
| `dkc` | The clock: fixed-rate frames |
| `dki` | Input: keyboard and mouse through DirectInput, a gamepad |
| `dkgl` | OpenGL helpers: projection, lights, screen-to-world |
| `dkt` | Textures (TGA) |
| `dkf` | Bitmap fonts |
| `dkp` | Particles |
| `dks` | Sound, on FMOD 3 |
| `dksvar` | Console variables: a name, a type, a range, a pointer to the value |
| `dko` | The DKO model format and its renderer |
| `baboNet` | The network: packets over TCP, with a UDP side for broadcasts |
| the game | Scene, menus, client, server, game rules, maps, editor, console |

The DLL split had a consequence the port had to respect: the engine and the game each compiled their
own copies of the basic classes (strings, vectors, matrices), and the copies are not the same. The
engine's vectors compare equal within 0.01, the game's within 0.0001. Each side's behaviour depends on its
own copy, so openbv keeps them apart by renaming the engine's
([Architecture](/internals/#two-copies-of-the-same-classes)).

At the top of the game is the *scene*. It owns the intro screen, the menus, the master-server
connection (the game list), and, once a game runs, a client, a server, or both. The client and the server
each own a *game*: the players, the projectiles, the map and the rules. When you host a game from the
menus, the process holds two complete games, the server's and the client's, and the client talks to the
server through the network layer as if it were far away. The dedicated server is the same code compiled
with the client parts left out.

There is one loop. `WinMain` sets up the window and the engine modules and runs the window's message loop;
every paint advances the scene by as many fixed steps as the clock says have passed and then draws once.
No part of the game blocks or runs on its own thread, except the HTTP requests to the account server,
which ran in threads of their own.

## Time

The game runs at a fixed 30 steps a second. The clock measures the real time since the last pass, adds it
to an accumulator and reports how many whole thirtieths of a second have gone by. The loop runs that many
updates, each with the same `delay` of 1/30 s, and then renders one frame. A slow machine draws fewer
frames but plays the same game; a stall is caught up by running several updates in a row.

Because every step is the same length, many timers in the game are counted in frames rather than seconds:
a ping is the number of frames until the pong arrives (one frame is about 33 ms), the server pings each
player every 30 frames, the photon rifle's beam hits again every third frame. This is the main reason the
Go server has to run the same fixed step: the rules are written in frames.

All randomness comes from the C library's `rand()`, through two helpers: one for integers in a range
(upper bound excluded) and one for floats, which draws one of 30,000 steps across the range. The Windows
build used MSVC's generator, a linear congruential one (×214013 + 2531011, the top 15 bits). The server
seeds it from the clock when it starts and again with every map, so the original is not repeatable from
run to run. The server uses it for shot spread, spawn points, ties in team assignment, the velocities of
dropped items and of molotov flames, and the map rotation's fallback; clients use it for effects.

openbv makes a run repeatable by giving the game a virtual clock in headless runs: the same input then
gives the same frames and sound on every runner. That is a property of the port, not of the original.

## The data

The game's data is a folder, `main/`, next to the executable, plus `bv2.db`:

| What | Where | Form |
|---|---|---|
| Maps | `main/maps/*.bvm` | A grid of cells and the spawn points (below) |
| Models | `main/models/*.DKO` | The engine's model format: weapons, flags, items, the bomb |
| Textures, skins | `main/textures/`, `main/skins/` | TGA images |
| Sounds, music | `main/sounds/` | WAV samples, two Ogg Vorbis tracks |
| Font | `main/fonts/babo.tga` | One bitmap with the glyphs |
| Text | `main/languages/*.lang` | The interface's strings per language |
| Settings | `main/bv2.cfg` | Console commands, run at start-up |
| Settings, master | `bv2.db` | A small SQLite database |

What is *not* data is as telling. The weapons, their damage, fire rate, spread and projectiles are a table
in the code, and so are the game types and their rules. The tuning knobs a server operator may turn are
console variables (`sv_*`: damage per weapon, respawn time, friendly fire, time and score limits, which
weapons are allowed), and the server sends their values to every client as console text
(`set sv_friendlyFire true`) when it joins. So a server can change the game's balance without shipping a
file, and every player plays by the server's numbers.

The configuration is a console script for the same reason: a setting is a console variable, so the menus,
the config file, a server's commands and a player's console all change it the same way.

Maps are small on purpose. A map is a grid of at most a few thousand cells, and a client that lacks the
server's map downloads it on joining, in 250-byte chunks at a rate the server caps (8 KB/s by default).
Custom maps could therefore spread through play. The game has its own map editor, in the main menu, that
writes the same format.

The DKO model format is used but not documented here: openbv runs the original's loader unchanged and has
not needed to describe it.

## The world

A map is a rectangle of square cells, one metre each. A cell is either floor or wall; a wall has a height,
and every cell also carries a "dirt" value, painted in the editor, that the renderer blends over the floor
texture. The map file adds a theme (grass, snow, sand, city and about twenty more: which textures to use),
a weather (none, fog, snow, rain, sandstorm, lava), the author's name, and per game type the places that
matter: the deathmatch spawn points, the two flag pods for capture the flag, the two bomb objectives and
the team spawns for search and destroy. The format has four versions, each adding fields; the newest
groups the per-game-type data into blocks.

The game is three-dimensional only in how it looks. Every babo is a sphere of radius 0.25 whose centre
stays at a height of 0.25: players move on the plane. Walls have heights, and those count where things
leave the plane. A ray test walks the grid cell by cell along the segment's main axis and checks each
cell's floor, top face and sides, so a grenade thrown in an arc can pass over a low wall and land beyond.
The same ray test serves bullets, projectiles, line of sight for explosions and the minibot's view.

Collision for a babo is simpler still: after it moves, its circle is pushed out of the neighbouring wall
cells along each axis, and the velocity along that axis is reversed and scaled down (to 45%), so babos
bounce off walls. Two babos that touch push each other apart the same way, but only once both have been
alive for three seconds, so a fresh spawn can't be shoved around. A babo that still ends up inside a wall
asks to respawn.

The source also has a path for full 3D maps made of a DKO model with a light map, ray-tested against the
model instead of the grid. Its directory name in the code is deliberately broken and no shipped map uses
it; the server ignores it.

## The gameplay

### The babo

A player is a babo: a ball with a gun. Its state is a *coord frame*: position, velocity, the point on the
map the mouse is over (which is where it aims), and a frame number. A babo has a primary weapon, a
secondary, two grenades and one molotov at spawn, and a life of 1.0 that weapons take fractions of.
After a spawn it is immune for two seconds.

The secondaries are the game's most distinctive weapons: popup knives that hit everyone within a metre,
an instant shield that halves damage for a moment, a nuke bot that sits where you drop it and explodes
after three seconds, and (in the Pro build) a minibot, a small turret that shoots at the nearest enemy in
sight for five seconds.

### Movement

Movement is acceleration and friction. Each held direction key adds 12.5 m/s² along that axis; the speed
loses 4 m/s every second; the speed is capped at 3.25 m/s. With the keys released a babo coasts to a stop
in under a second, which gives the game its slippery, rolling feel. On snow maps with the `sv_slideOnIce`
rule, dirty cells are ice: acceleration drops to 4 and friction to 1, and babos skate.

Being hit pushes you: when a shot hits your babo, your client adds a push along the shot's direction,
proportional to the weapon's damage. Since the client owns its babo's movement (below), knockback is a
client-side effect.

The babo is drawn rolling: each frame its orientation turns about the axis perpendicular to its motion by
an angle proportional to the distance moved. The rotation is a matrix, renormalised each time because
accumulated rounding shrank the ball (the source's comment says so).

### Weapons and damage

There are two kinds of weapon. *Hitscan* weapons (the submachine gun, shotgun, sniper rifle, dual machine
guns, chain gun, photon rifle, flame thrower) hit along a line at once. *Projectiles* (the rocket, the
grenade, the molotov) are objects that fly, bounce and explode.

Hitscan shots have spread that grows as you fire: each shot adds to the weapon's current imprecision, up to
its maximum, and it decays back over time, so short bursts are accurate and long ones are not. The shotgun
fires five pellets in a fan. The sniper rifle fires two shots in one, three when the camera is zoomed out
far enough. The photon rifle charges, then leaves a beam that hits everyone on it and keeps hurting for a
second. The flame thrower's reach shrinks the longer you hold the trigger, and on servers that enable it,
overheating it makes you explode. The chain gun overheats and must cool down.

Explosions damage everyone within their radius who is in line of sight of the centre, falling off
linearly with distance: grenades and rockets over three metres, the nuke over six. Your own explosions hurt
you. Molotovs leave flames that stick to a babo that walks into them and burn whoever is near.

A death drops the victim's weapon, a life pack and the grenades they had left. Walking over a life pack or
a grenade picks it up; a weapon is picked up with a key, swapping it for yours. This is how a player gets a
weapon other than the one chosen at spawn.

Damage is modified in a fixed order: the server's per-weapon value (Pro rules), distance falloff for the
photon rifle and flame thrower (Pro rules), half with a shield, none while immune, all of it under the
Instagib sub-type. Friendly fire is off by default; with *reflected damage* on, a shot at a teammate hurts
the shooter instead.

### The client and the server

Who decides what is the centre of the design, and it is not what a modern shooter would do. Babo Violent
2 trusts the client with movement and the server with everything that hurts:

| Concern | Decided by |
|---|---|
| Your babo's movement and its collision with walls | Your client |
| Where other babos are | Each machine, by interpolating the frames it receives |
| A hitscan shot | Your client sends the origin and direction; the server adds the spread, traces it and picks the victim |
| Projectiles | The server creates, flies and explodes them; clients fly copies for display |
| Damage, health, death | The server |
| Kills, deaths, scores | The server, and every client recomputes them on its own |
| Spawn points, teams, flags, the end of a round, the next map | The server |
| Chat | Relayed by the server, which filters team messages |

Each client simulates its own babo and sends a coord frame to the server every few frames; the server
relays the frames to everyone else. Positions travel in hundredths of a metre as 16-bit integers,
velocities in tenths as single bytes. How often a client sends depends on its ping: a player with a 100 ms
ping sends about every third frame, so a slow connection is not flooded.

A machine that receives frames for a babo doesn't snap it to them. It keeps the last two, and over the
frames between them it moves the babo along a cubic curve whose ends follow the two velocities, so the
motion stays smooth through the gaps. When the next frame is late it extrapolates with the last velocity
for up to half a second, then freezes the babo where it was last seen. The server does the same for every
player, and that interpolated position is the one it hits: a shot is tested against the target as the
server currently imagines it.

Shooting is a request. The client sends where it fired from and in which direction; the server checks that
the rate is plausible, adds the random spread, traces the ray against the map and the players, and
broadcasts the result: from where to where, whom it hit. Every client draws the trail and impact from that
answer. When the server decides a hit, it sends the victim's *remaining* life, not the damage. Each client
then sees whether that life is gone and does the kill bookkeeping itself, with the same rules as the
server: kills, deaths, score and team score. No message carries a score. It is cheap, and it works
because every machine runs the same rules on the same messages, which is why the Go server must send
exactly the hits the original would.

The server's protection against cheating is light and fits this split. It compares the pace of a client's
frame numbers with its own and the reported speed with the maximum, and disconnects a player who exceeds
either three checks in a row. It limits fire rates loosely, with tolerances. It checks the weapons a
player spawns with. Melee secondaries have no rate check at all.

Joining is a short exchange: the server announces the new player to all, the client checks the protocol
version and sends its name, the server checks the password and then sends a dump of the state: the
server's settings, the round state, every other player, every live projectile and the flags. A client
that lacks the map asks for it first and repeats the exchange after the download.

The original sent all of this over TCP; the "unreliable" messages only asked for UDP, which the server
never enabled. openbv carries the same packets over WebSockets, which are just as reliable and ordered
([Networking](/internals/networking)).

### Spawning, teams and balance

A dead player chooses a primary and a secondary and asks to spawn once the respawn delay has passed (the
client waits; the server doesn't check). The server picks the spot: for each spawn point it finds the
nearest living enemy, and it takes the spawn point whose nearest enemy is furthest away. With nobody else
alive the choice is random. It is a simple rule that keeps people from spawning in front of a gun.

Team requests go through the server, which can also assign automatically: to the smaller team, or the
losing one, or at random. In team games an automatic balance moves players when the teams differ by two or
more, after a warning, choosing the player who has played least this game and never a flag carrier.

### Game modes and scoring

| Mode | Played to | Score |
|---|---|---|
| Deathmatch | A score limit or the time limit | A kill +1, a suicide −1 |
| Team deathmatch | A team score limit or the time limit | A kill +1 for the killer's team; a team kill −1 |
| Capture the flag | A number of captures or the time limit | A capture +1; kills don't count toward the team |
| Search and destroy / Champion | Rounds, or the time limit | (below) |

Capture the flag works on distances, checked every server frame: an enemy within a quarter of a metre of
your flag's pod takes it; your flag on the ground is returned by a teammate who touches it and taken again
by an enemy who does; a carrier scores by reaching their own pod while their own flag is at home. A
carrier who dies drops the flag where they fell. The touch radii are not all equal (one of the ground cases
uses half a metre), a quirk the port keeps.

Two sub-types change any mode: Instagib (every hit kills, except grenades, knives and molotovs) and random
weapons.

The fourth mode is the least understood. The source still has search and destroy (bombs, objectives, team
spawns in the map format, a bomb timer), but the Pro build that 2.11 is replaces it with a mode called
"Champion": players of a team keep a slot that picks their group of spawn points, a round timer kills
everyone when it runs out (on the clients; the server's babos stay alive), and since nothing counts wins
in it, a game ends on time as a draw. How it was meant to be played isn't clear from the source.

When a game ends, the result shows for ten seconds and the next map loads. The rotation prefers the map
not played for the longest whose size suits the number of players, measured in floor cells per babo.

### Votes and administration

Players can call votes on the commands the server's operator has allowed (`voteon changemap`, for
example). A vote passes with a majority of the players on teams, within thirty seconds. Operators run the
server from its console: kick, ban by IP, move players between teams, change the map, change any `sv_`
variable; in the Pro build an in-game admin, after logging in, can send console commands from the game.

## Saving

There is no saved game: a round lives on the server. What persists is the player's configuration
(`bv2.cfg`: name, skin, colours, keys, the console variables), `bv2.db`, the maps a player made or
downloaded, and on a server its ban list. In openbv these are written to gasm storage, where they shadow
the shipped files.

## Presentation

The camera looks almost straight down on the map from a height that the mouse wheel changes; it eases
toward its target each frame. With the sniper rifle the camera rises the further from the babo you aim,
which is how sniping works: you see further by aiming further (and the server, which trusts the camera
height a client reports, gives the zoomed-out shot more damage). Servers may switch to a tilted
perspective instead (`sv_topView`).

The renderer is fixed-function OpenGL of its time. At load, the map becomes meshes: the floor in the
theme's textures with the dirt blended over it, the walls as extruded blocks with their sides shaded by
direction, and a shadow mesh cast by the walls. Each frame draws the floor, the floor marks (blood and
scorch marks, kept in a fixed ring), the shadows of babos and projectiles, the babos, the projectiles and
the spent shell casings, the walls, the bullet trails, the particles (smoke, sparks, blood, flames, all
from the engine's particle module), the weather and finally the minimap and the HUD. On rainy and snowy
maps an option draws the whole scene a second time, mirrored under the floor and blended, so the ground
reflects. Fog comes from the weather.

A babo is a textured sphere. Its 64×32 skin is made when it spawns: each pixel of the chosen skin image
is a mix of red, green and blue, and those three channels are replaced by three colours, the player's own
in deathmatch or the team's red or blue shades in team games. A few skin images thus give every player a
look of their own while keeping teams readable.

Sound is FMOD 3 through the engine's `dks`: each effect is played at a point on the map with a range, and
the listener follows the camera, so gunfire is louder and panned toward where it happens. Some sounds a
client makes for its own actions (the photon rifle charging, the chain gun overheating) are sent to the
server, which relays them to the others. The menus and the game each have one Ogg Vorbis music track.

The interface is the game's own: bitmap-font text, its own widgets, and a console (the backquote key) where
every setting is a command. openbv keeps the original HUD and redraws the menus in a flat style of its own,
on a 16:9 canvas; that is the port's choice, not the game's.

## Lessons for building such a game

- **A fixed step makes the rules simple.** With every update 1/30 s long, timers can count frames and the
  server and the clients agree on what a second is, without passing time around.
- **Trust the client for feel, the server for harm.** Moving your own babo locally means no input lag at
  any ping; deciding shots and damage on the server keeps fights fair enough. The cost is that a modified
  client can move as it likes, which the speed check only partly covers.
- **Send results, recompute the rest.** Sending the victim's remaining life and letting every machine do
  its own kill bookkeeping keeps the traffic small, but it only works while every machine runs exactly the
  same rules.
- **Interpolate, don't snap.** A cubic curve between the last two received states, with the velocities as
  tangents, turns sparse network frames into smooth motion, and testing hits against that same curve on
  the server keeps what you see close to what counts.
- **Make tuning a console variable.** Putting weapon damage and rules behind server variables that are
  sent to clients let operators balance the game without patches and let one build serve very different
  servers.
- **Keep maps small enough to send.** A cell grid of a few kilobytes, downloaded on join, let player-made
  maps spread without a distribution channel.
- **Recolour, don't redraw.** Three colour masks in each skin image give endless player and team colours
  from a handful of textures.
- **Choose spawns by distance.** Spawning where the nearest enemy is furthest away is one loop over the
  spawn points and avoids most spawn kills.

## Further reading

- [`design/server.md`](https://github.com/emdzej/openbv/blob/main/design/server.md): the original server's
  rules, messages, formulas and quirks, with references to the source.
- [Architecture](/internals/) and [Networking](/internals/networking): what openbv replaced below the game.
- [`server/README.md`](https://github.com/emdzej/openbv/blob/main/server/README.md): the Go server, and
  where it deliberately differs from the original.
- The original source: [Daivuk/BaboViolent2](https://github.com/Daivuk/BaboViolent2).
