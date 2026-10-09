# openbv-server

Babo Violent 2.11 game servers for the openbv client, in Go: one process runs several sessions (each a
game server of its own, with its port, settings and players), with an admin page. The rules are a port of
the original's `Server*.cpp` and the parts of the game it runs; `design/server.md` is the specification.

**State:** milestone 1 of `design/server.md` §10.2: players connect, get the original's handshake and state
dump (or download the map), choose teams, spawn, move, see each other and chat; pings, timeouts, idling,
the join message, round end and map rotation. Shooting, damage, projectiles, CTF, votes and the master
server (the in-game Game Browser) come next. Their messages are accepted and ignored.

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
| `internal/bvmath` | float32 vectors, `cubicSpline`, MSVC's `rand()` and the game's `rand` helpers |
| `internal/bvmap` | `.bvm` maps, all four versions |
| `internal/cvar` | The `sv_*` variables, their text formats and the engine's parsing |
| `internal/game` | The server: the frame (`Server::update`), the messages (`Server::recvPacket`), players |
| `internal/session` | Sessions: the 30 Hz loop, the manager with per-session ports |
| `internal/admin` | The admin API and page |

## Test

```sh
go vet ./... && go test ./...
```

The tests that need the game's maps use `../ref/BaboViolent2/BaboViolent2/Content` (fetched by `./play`)
and skip without it. `internal/game` runs the protocol end to end over a WebSocket: the handshake, a
refused password, teams, spawning and two players seeing each other move.

Against the original: the openbv client logs every packet with `--param netlog=1`. Hosting a game in the
client (the original C++ server, in-process) and joining the Go server give the same message sequence
(design/server.md §10.3).
