# openbv-server

Babo Violent 2.11 game servers for the openbv client, in Go: one process runs several sessions (each a
game server of its own, with its port, settings and players), with an admin page. The rules are a port of
the original's `Server*.cpp` and the parts of the game it runs; `design/server.md` is the specification.

**State:** milestones 1 to 4 of `design/server.md` §10.2. Players connect, get the original's handshake and
state dump (or download the map), choose teams, spawn, move, see each other and chat; pings, timeouts,
idling, the join message, round end and map rotation. Deathmatch combat: the hitscan weapons (SMG, shotgun,
sniper, dual machine gun, chain gun, photon rifle with its lingering beam, flame thrower) with the
original's spread, fire-rate checks and ray tests; damage with the Pro values, shields, spawn immunity and
instagib; kills, scores, the drops (life pack, weapon, grenades) and their pickups; the `sv_serverType = 1`
quirk. Projectiles and secondaries: rockets (remote detonation), grenades, molotovs and their flames
(sticking to players, burning), radius damage, the knives, the shield, the nuke bot, the minibot turret
(its aim, shots and wall collisions, its coord frames), `sv_explodingFT`. The team modes: team deathmatch,
capture the flag (taking, dropping, returning and capturing flags, with the original's radii), "Champion"
(type 3, Pro: its spawn slots and round resets), auto-balance, auto-assign, team spawns and scores, and each
type's round end. Votes, the admin commands and the master server (the in-game Game Browser) come next; their
messages are accepted and ignored.

## Run

```sh
go build -o openbv-server ./cmd/openbv-server
OPENBV_CONTENT=../ref/BaboViolent2/BaboViolent2/Content OPENBV_ADMIN_TOKEN=change-me \
  OPENBV_SESSION='{"name":"My server","gameType":0,"port":3333,"maps":["DM-MiniArena","CTF-Daivuk"]}' \
  ./openbv-server
```

Then in the game: open the console (backquote) and `connect <host> 3333`, or `./play --master=...` once
the master exists. The client connects to `ws://<host>:<port>/`.

| Variable | |
|---|---|
| `OPENBV_LISTEN` | The HTTP address of the admin page and the shared game endpoint (default `:8080`) |
| `OPENBV_ADMIN_TOKEN` | The admin page's token (required to use it) |
| `OPENBV_CONTENT` | The game's content folder (`bv2.db`, `main/`): maps come from `main/maps` (default `content`) |
| `OPENBV_SESSION` | A session to start at boot, as JSON (below) |
| `OPENBV_MAX_SESSIONS` | Sessions at most (16) |
| `OPENBV_ORIGINS` | Browser origins allowed to connect (the web player), comma-separated |
| `OPENBV_DEBUG` | `1`: debug logs; `2`: also every packet sent and received |

A session: `name` (sv_gameName), `password` (sv_password, at most 15 characters), `gameType` (0 free for all,
1 team deathmatch, 2 capture the flag, 3 Champion), `maxPlayers` (1–32), `maps` (the rotation; the first is
played first), `port` (the session listens there).

## Endpoints

| Path | |
|---|---|
| `ws://<host>:<session port>/` | A session's game connections (each session listens on its own port, as an original server did) |
| `GET /bv2/{session}` (on `OPENBV_LISTEN`) | The same, for deployments behind one reverse-proxied port (the client accepts a `ws://`/`wss://` URL as host) |
| `/admin/` | The admin page: sessions (create, stop) and their players |
| `GET/POST /admin/api/sessions`, `GET/DELETE /admin/api/sessions/{id}` | The admin API (`Authorization: Bearer <token>`) |
| `GET /healthz` | `ok` |

## Layout

| Package | |
|---|---|
| `internal/wire` | baboNet packets over WebSockets: `u16 type, u8 protocol, u8 0, payload` |
| `internal/transport` | WebSocket connections: reader, bounded send queue, pings |
| `internal/bbnet` | baboNet's server semantics: one event per update, admission at half-second checks, NetIDs, packet order |
| `internal/proto` | Every message struct, byte for byte (checked against `design/server.md` Appendix A) |
| `internal/bvmath` | float32 vectors (no fused operations), `rotateAboutAxis` with musl's `cosf`/`sinf`, `segmentToSphere`, `cubicSpline`, MSVC's `rand()` and the game's `rand` helpers |
| `internal/bvmap` | `.bvm` maps, all four versions; `rayTest`; the cell collisions (`performCollision`, `collisionClip`) |
| `internal/cvar` | The `sv_*` variables, their text formats and the engine's parsing |
| `internal/game` | The server: the frame (`Server::update`), the messages (`Server::recvPacket`), players, weapons, shooting and damage (`combat.go`), projectiles (`projectile.go`), the minibot and nuke bot (`minibot.go`) |
| `internal/reftest` | Values from the original C++ for the tests: `cpp/build.sh` compiles the original functions (extracted verbatim) and writes `testdata/golden.json.gz` and `testdata/projectiles.json.gz` |
| `internal/session` | Sessions: the 30 Hz loop, the manager with per-session ports |
| `internal/admin` | The admin API and page |

## Test

```sh
go vet ./... && go test ./...
```

The tests that need the game's maps use `../ref/BaboViolent2/BaboViolent2/Content` (fetched by `./play`)
and skip without it. `internal/game` runs the protocol end to end over a WebSocket: the handshake, a
refused password, teams, spawning and two players seeing each other move.

Against the original (design/server.md §10.3):

- `internal/reftest`: the bit-exact core — ray tests, segment-to-sphere, rotations, the bullet spread with
  its `rand()` calls, bounces, the damage formula — against values computed by the original code
  (`internal/reftest/cpp/build.sh` regenerates them; needs clang, python3 and unifdef; CI only reads them).
- `internal/game/projectile_ref_test.go`: the original `Projectile::update`, `Game::radiusHit`,
  `CMiniBot::Think`, `Game::shootMinibotSV` and the map collisions, compiled natively with stand-ins that
  record what they send (`cpp/proj_head.cpp`, `cpp/proj_main.cpp`): 700 projectiles of every kind run
  frame by frame (40,102 frames: every message's bytes, every radius hit, the state, the rand() state),
  400 minibots, 600 radius hits, 1,500 collisions — bit for bit. `internal/game/secondary_test.go` checks
  the request paths (remote detonation, knives, the nuke's timer, the minibot, the shield, a molotov).
- `internal/game/team_ref_test.go`: the original `Server::updateCTF`, `Server::autoBalance` with
  `Server::update`'s auto-balance and type-3 blocks, `Game::assignPlayerTeam`, `Game::spawnPlayer` and
  `Player::kill` (`cpp/team_head.cpp`, `cpp/team_main.cpp`): 600 CTF cases over scripted paths (takes,
  returns, captures, drops), 500 auto-balance runs, 1,500 team assignments, 2,000 spawn choices across the
  game types, 200 type-3 round resets — bit for bit, rand() state included.
- `internal/game/listen_test.go`: sniper shots recorded from the C++ listen server (a game hosted in the
  client, `--param netlog=1`), each reply reproduced byte for byte.
- By hand: the client logs every packet with `--param netlog=1`. Hosting a game in the client (the
  original C++ server, in-process) and joining the Go server give the same message sequence. For a fight,
  run two clients (`./play --realtime --profile=a` and `--profile=b`) against one session.
