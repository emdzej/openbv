# openbv-server

Babo Violent 2.11 game servers for the openbv client, in Go: one process runs several sessions (each a
game server of its own, with its port, settings and players), with an admin page. The rules are a port of
the original's `Server*.cpp` and the parts of the game it runs; `design/server.md` is the specification.

**State:** milestones 1 to 5 of `design/server.md` §10.2. The game: connections, the original's handshake
and state dump, map downloads, pings, timeouts and idling, chat, teams and spawns, coord frames, round end
and map rotation; deathmatch combat (every hitscan weapon with the original's spread and ray tests,
damage with the Pro values, kills, drops and pickups, the `sv_serverType = 1` quirk); projectiles and
secondaries (rockets, grenades, molotovs and flames, knives, the shield, the nuke bot, the minibot); the
team modes (team deathmatch, capture the flag, "Champion", auto-balance). Administration: votes, the
dedicated server's console commands, bans by address and MAC (shared by the process's sessions), the
in-game admin login and its console. The product around it: sessions saved and restored, settable live
(any `sv_*`), restartable; an admin page (sessions, players, console and log, variables, rotation, bans,
audit log) with a token or OIDC sign-in; rate limits; the master for the game's Game Browser.

## Run

```sh
go build -o openbv-server ./cmd/openbv-server
OPENBV_CONTENT=../.deps/content OPENBV_DATA=./data OPENBV_ADMIN_TOKEN=change-me \
  OPENBV_SESSION='{"name":"My server","gameType":0,"port":3333,"maps":["DM-MiniArena","CTF-Daivuk"]}' \
  ./openbv-server
```

(`tools/fetch-content.sh` fetches the vanilla content into `../.deps/content`.) Then open
`http://localhost:8080/admin/`, or play: `./play --master=127.0.0.1:10207` lists the sessions in the Game
Browser, or `connect 127.0.0.1 3333` in the game's console. The container image: `server/Dockerfile`
(build from the repository's root), published as `ghcr.io/emdzej/openbv-server` on releases.

| Variable | |
|---|---|
| `OPENBV_LISTEN` | The HTTP address: admin page, `/master`, `/bv2/{session}`, `/bv2/port/{port}` (`:8080`) |
| `OPENBV_CONTENT` | The game's content folder (`bv2.db`, `main/`): maps from `main/maps` (`content`) |
| `OPENBV_DATA` | `sessions.json`, `bans.json`, `audit.log` (empty: in memory) |
| `OPENBV_ADMIN_TOKEN` | The admin page's token (with OIDC: for scripts) |
| `OIDC_ISSUER`, `OIDC_CLIENT_ID`, `OIDC_ROLE` | OIDC sign-in for the admin page (a public client; PKCE) |
| `OPENBV_MASTER_PORT` | The master's port (`10207`; `0`: only `/master`) |
| `OPENBV_MASTER_REGISTER` | `1`: other servers may register with this master |
| `OPENBV_CONN_PER_MINUTE` | New game connections per address and minute (`30`) |
| `OPENBV_TRUST_PROXY` | `1`: player addresses from `X-Forwarded-For` |
| `OPENBV_MAX_SESSIONS` | Sessions at most (`16`) |
| `OPENBV_ORIGINS` | Browser origins allowed to connect (the web player) |
| `OPENBV_SESSION` | A session to create when none is saved, as JSON (`session.Settings`) |
| `OPENBV_DEBUG` | `1`: debug logs; `2`: also every packet |

A session (`internal/session.Settings`): `name`, `password` (at most 15 characters), `gameType` (0 free for
all, 1 team deathmatch, 2 capture the flag, 3 Champion), `maxPlayers` (1–32), `maps` (the rotation),
`port`, `adminUser`/`adminPassword` (the in-game admin login: `admin <user> <password>` in the game's
console, then `- <command>`), `voteOn` (votable commands), `cvars` (any other `sv_*`/`zsv_*`, strictly
parsed), `private` (not in the Game Browser).

## Endpoints

| Path | |
|---|---|
| `ws://<host>:<session port>/` | A session's game connections (each session listens on its own port, as an original server did) |
| `/bv2/{session}`, `/bv2/port/{port}` (on `OPENBV_LISTEN`) | The same, behind one reverse-proxied port (the client takes a `ws://`/`wss://` URL as host) |
| `ws://<host>:10207/`, `/master` | The master (the Game Browser's list): BV2_LIST → MASTER_INFO + BV2_ROW |
| `/admin/` | The admin page |
| `/admin/api/...` | The admin API (`Authorization: Bearer <token>`): `sessions` (GET, POST), `sessions/{id}` (GET, PUT: settings and restart, DELETE: stop), `sessions/{id}/restart`, `/command {line}`, `/say {text, player?}`, `/cvars` (GET; POST `{name, value}`), `/maps {action: add/remove/change/next, map}`, `/players/{id}/kick`, `/ban`, `/move {team}`, `/log?since=`; `maps`, `bans` (GET, POST `{name, ip, mac}`), `bans/{index}` (DELETE), `audit`, `me`; `config` (no auth: how the page signs in) |
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
| `internal/game` | The server: the frame (`Server::update`), the messages (`Server::recvPacket`), players, weapons, shooting and damage (`combat.go`), projectiles (`projectile.go`), the minibot and nuke bot (`minibot.go`), the team modes (`team.go`), votes and the admin login (`admin.go`), the console commands (`console.go`) |
| `internal/reftest` | Values from the original C++ for the tests: `cpp/build.sh` compiles the original functions (extracted verbatim) and writes `testdata/golden.json.gz` and `testdata/projectiles.json.gz` |
| `internal/session` | Sessions: the 30 Hz loop, the manager (per-session ports, saved and restored sessions, restarts, connection rate limits), each session's log ring |
| `internal/store` | `sessions.json`, `bans.json`, `audit.log` |
| `internal/master` | The game list for the client's Game Browser, and registrations from other servers |
| `internal/admin` | The admin API and page (`static/`: vanilla JS, no build step), token and OIDC sign-in |

## Test

```sh
go vet ./... && go test ./...
```

The tests that need the game's maps use `../ref/BaboViolent2/BaboViolent2/Content` (fetched by `./play`)
and skip without it (`OPENBV_TEST_CONTENT` points elsewhere: CI uses `.deps/content`). `internal/game` runs
the protocol end to end over a WebSocket: the handshake, a refused password, teams, spawning and two
players seeing each other move; `admin_test.go` the votes (passing, timing out, cancelled), the admin
login and its console, `set`, bans, the rotation and moves. `internal/admin` drives the API with
`httptest` against a stub game; `internal/master` the list over a WebSocket and the rows' bytes;
`internal/store` the files.

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
