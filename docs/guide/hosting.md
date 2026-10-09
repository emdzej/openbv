# Hosting a game

Hosting works as it did on Windows: the server runs inside the game, and you join it.

1. In the main menu, open **Host**.
2. Pick the game type (free for all, team deathmatch, capture the flag, champion), the maps (none
   ticked: CTF-Daivuk) and the limits, then **Start**.
3. In the game's menu, pick a weapon and a secondary, then **Auto assign team** (or a team).
4. Shoot to spawn.

The console (backquote) shows what the server does. The same server commands as the original work
there; `?` lists them.

## Playing with others

Not yet. A hosted game is reachable only by the game itself: gasm guests talk to the network
through WebSockets, so they can connect to servers but can't listen for players. Online play goes
through the openbv server instead (Go, many sessions at once, an admin page), which is in progress.
See [Networking](/internals/networking) for how the game connects and [Status](/status) for where it
stands.

## The master server

The in-game Game Browser lists games from a master server. The one in the original `bv2.db`
(RndLabs', `78.46.36.43`) is gone, so openbv asks none by default; pass one with
`./play --master=host:port` (for gasm-run: `--param master=host:port`) once there is one to ask.
