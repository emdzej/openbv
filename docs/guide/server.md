# Running a server

`openbv-server` hosts Babo Violent 2 games for openbv players. One process runs several **sessions**:
each is a game server of its own, with its port, its settings, its map rotation and its players. An
admin page creates and runs them, and the same process is the **master**, the game list the client's
Game Browser shows.

The game rules are a Go port of the original's server code (`Server*.cpp` and the simulation it shares
with the game). The specification is
[design/server.md](https://github.com/emdzej/openbv/blob/main/design/server.md); deviations from the
original are listed there (§8.4).

## Get it

From the [releases](https://github.com/emdzej/openbv/releases): `openbv-server-<version>-<os>-<arch>`
for Linux (x86_64, arm64), macOS (Apple Silicon, Intel) and Windows, or the container image:

```sh
docker run -d --name openbv -p 8080:8080 -p 10207:10207 -p 3333:3333 \
  -e OPENBV_ADMIN_TOKEN=change-me -v openbv-data:/data ghcr.io/emdzej/openbv-server
```

The image carries the vanilla 2.11 maps. The binaries need the game's content folder (the server reads
the maps from `main/maps`): a copy of the game, or `tools/fetch-content.sh` from the repository.

```sh
OPENBV_CONTENT=/path/to/content OPENBV_DATA=./data OPENBV_ADMIN_TOKEN=change-me ./openbv-server
```

Then open `http://<host>:8080/admin/`, sign in with the token and create a session.

## Configuration

| Variable | |
|---|---|
| `OPENBV_LISTEN` | The HTTP address: the admin page, `/master` and the shared game endpoints (`:8080`) |
| `OPENBV_CONTENT` | The game's content folder (`content`) |
| `OPENBV_DATA` | Where the server keeps `sessions.json`, `bans.json` and `audit.log`; empty: in memory |
| `OPENBV_ADMIN_TOKEN` | The admin page's token (with OIDC: for scripts) |
| `OIDC_ISSUER`, `OIDC_CLIENT_ID`, `OIDC_ROLE` | Sign in to the admin page with an OpenID Connect provider (a public client, authorization code + PKCE); `OIDC_ROLE` is the role an admin needs |
| `OPENBV_MASTER_PORT` | The master's own port (`10207`; `0`: only `/master` on `OPENBV_LISTEN`) |
| `OPENBV_MASTER_REGISTER` | `1`: other servers may list themselves with this master |
| `OPENBV_CONN_PER_MINUTE` | New game connections per address and minute (`30`; `0`: no limit) |
| `OPENBV_TRUST_PROXY` | `1`: take players' addresses from `X-Forwarded-For` (only behind a proxy that sets it) |
| `OPENBV_MAX_SESSIONS` | Sessions at most (`16`) |
| `OPENBV_ORIGINS` | Browser origins allowed to connect (the web player), comma-separated |
| `OPENBV_SESSION` | A session to create when none is saved, as JSON (below) |
| `OPENBV_DEBUG` | `1`: debug logs, `2`: every packet too |

## Sessions

A session's settings: a name, the game type (free for all, team deathmatch, capture the flag,
Champion), the most players, its port, the map rotation, a password, the in-game admin login, the
commands players may vote on, any other `sv_*` variable, and whether the Game Browser lists it.

```json
{ "name": "My server", "gameType": 2, "port": 3333, "maxPlayers": 16,
  "maps": ["CTF-Daivuk", "CTF-Bites"], "password": "",
  "adminUser": "boss", "adminPassword": "...", "voteOn": ["kick", "changemap"],
  "cvars": { "sv_scoreLimit": "30", "sv_friendlyFire": "true" } }
```

The admin page shows a session's players (kick, ban, move to a team, chat), its console (the log, and
the original's server commands: `playerlist`, `changemap`, `addmap`, `voteon`, `set sv_...`, `banlist`,
`help` for the rest), its variables, its rotation and its settings. Saving settings restarts the session.
What the console changes (`set`, the rotation, `voteon`) is kept with the session. Sessions are saved in
`OPENBV_DATA` and come back when the server restarts.

Bans are shared by every session of the server and match the player's address or the client's MAC (a
random one per openbv installation). The audit log records every admin action: who, what, when.

**In-game admins.** With `adminUser` and `adminPassword` set, a player types `admin <user> <password>` in
the game's console (the backquote key), then `- <command>` runs a server command, and the server's
console messages show up there, as in the original.

**Votes.** Players vote with the console's `vote <command>`, for commands the session allows
(`voteOn`, or the console's `voteon`). A vote passes with more than half of the playing players, within
30 seconds.

## Players connecting

Each session listens on its own port, as an original server did: players join with the Game Browser,
or with `connect <host> <port>` in the game's console (openbv connects to `ws://<host>:<port>/`). Open
those ports in your firewall.

Behind a reverse proxy with one port, sessions are also at `/bv2/<session id>` and `/bv2/port/<port>`
on `OPENBV_LISTEN`; players connect with a URL: `connect wss://example.org/bv2/port/3333`.

## The Game Browser

The client asks the master given at launch: `./play --master=<host>:10207`, or the launch parameter
`master=<host>:<port>` with any gasm runner. Without one, the Game Browser stays empty (the original
master is gone). The list has the server's public sessions; the client joins them at the master's host.
Players' pings show as `???`: the original measured them with UDP, which openbv doesn't have.
