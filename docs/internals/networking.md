# Networking

The original baboNet (`src/engine/babonet`) framed the game's packets over TCP, with an optional UDP
side, and added a peer-to-peer UDP part for LAN broadcasts, server pings and remote administration.
gasm guests have WebSockets only (`gasm:net`: reliable, ordered, binary messages; browsers can't open
raw sockets), so `src/port/babonet_gasm.cpp` implements baboNet's API (`src/inc/baboNet.h`) on them.

## Clients

`bb_clientConnect(host, port)` opens `ws://<host>:<port>/`, or the URL itself when the host is a
`ws://` or `wss://` URL. Each baboNet packet is one binary message:

| Bytes | Field |
|---|---|
| 0–1 | packet type ID, unsigned 16-bit little-endian (the game's `NET_*` IDs) |
| 2 | protocol the game asked for: 0 TCP (reliable), 1 UDP (unreliable) |
| 3 | 0 (reserved) |
| 4… | the packet's bytes, as the game laid them out (`netPacket.h`) |

Over a WebSocket everything is reliable and ordered; the protocol byte only tells the server what
the game meant. `bb_clientUpdate` reports the connection as the original did: 3 once connected, 1 if
it failed, 2 when the server closed it.

## Hosted games

A browser can't listen, so `bb_serverCreate` makes an in-process server, and the game's own client
reaches it by connecting to the local address (`127.0.0.1`, `localhost`, the empty host or the
game's own IP) and the server's port. That is how a hosted game ran on Windows too: client and
server in one process. The original's rules are kept: `bb_serverUpdate` reports one event per call
(a new client's ID, a lost client's ID negated), new connections are accepted at its half-second
checks, packets come out oldest first, and a received packet stays valid until the next receive on
the same side.

## What has no transport

The peer-to-peer UDP part: sends go nowhere and nothing is received. That covers the LAN game
search, the Game Browser's pings and the remote admin tool. `bb_getMyMAC`, which the game sends
with a player's info (servers ban by it), gives a made-up address kept in storage, the same on every
run of one installation.

## The master server

The Game Browser asks a master server for games. `bv2.db` lists RndLabs' (`78.46.36.43`, port 11207,
which the game uses minus 1000), long gone. openbv uses the one given as the launch parameter
`master=host:port` instead, or none: an empty address, which fails at once without asking the player
about a host.

## The openbv server

In progress: a server in Go that speaks this wire format, hosts many game sessions in one process,
lists them for the Game Browser, and has an admin page (players, kicks and bans, map rotation,
settings, chat).
