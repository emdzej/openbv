// Package game is Babo Violent 2's server rules, ported from the original's Server*.cpp and the
// simulation it shares with the client (design/server.md is the specification).
//
// For now it only records what clients send, which lets a real client's traffic be checked against
// the specification while the port is written.
package game

import (
	"encoding/hex"
	"log/slog"

	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/transport"
	"github.com/emdzej/openbv/server/internal/wire"
)

// Game is one session's rules.
type Game struct {
	settings session.Settings
	content  string
	log      *slog.Logger
	conns    map[uint32]*transport.Conn
}

// New makes the rules for a session; content is the game's folder (bv2.db, main/).
func New(s session.Settings, content string, log *slog.Logger) (*Game, error) {
	return &Game{settings: s, content: content, log: log, conns: map[uint32]*transport.Conn{}}, nil
}

func (g *Game) Join(c *transport.Conn) {
	g.conns[c.ID] = c
	g.log.Info("client connected", "netId", c.ID, "remote", c.Remote)
}

func (g *Game) Packet(c *transport.Conn, p wire.Packet) {
	n := len(p.Data)
	if n > 64 {
		n = 64
	}
	g.log.Debug("packet", "netId", c.ID, "type", p.Type, "proto", p.Protocol, "size", len(p.Data), "head", hex.EncodeToString(p.Data[:n]))
}

func (g *Game) Leave(c *transport.Conn) {
	delete(g.conns, c.ID)
	g.log.Info("client left", "netId", c.ID)
}

func (g *Game) Tick(dt float64) {}

func (g *Game) Players() []session.PlayerInfo {
	out := []session.PlayerInfo{}
	for _, c := range g.conns {
		out = append(out, session.PlayerInfo{NetID: c.ID, Remote: c.Remote})
	}
	return out
}

func (g *Game) Close() {}
