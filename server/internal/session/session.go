// Package session runs game sessions: each one is a Babo Violent 2 server of its own (its port, its
// settings, its players) with a single goroutine that owns all its state and steps the game at the
// original's fixed 30 Hz (dkcInit(30)), catching up after a stall as the original's main loop did.
// Connections only queue packets (internal/bbnet); the game reads them during its frame.
package session

import (
	"context"
	"log/slog"
	"time"

	"github.com/emdzej/openbv/server/internal/bbnet"
)

// TickRate is the original's fixed update rate.
const TickRate = 30

// maxCatchUp bounds the frames run back to back after a stall.
const maxCatchUp = 10

// Settings are what the admin page sets when creating a session.
type Settings struct {
	Name       string   `json:"name"`     // sv_gameName
	Password   string   `json:"password"` // sv_password
	GameType   int      `json:"gameType"` // sv_gameType
	MaxPlayers int      `json:"maxPlayers"`
	Maps       []string `json:"maps"` // the rotation, by name without .bvm
	Port       int      `json:"port"` // sv_port: the session listens there
}

// Game is the rules side of a session (internal/game). All calls come from the session's goroutine.
type Game interface {
	Frame()
	Players() []PlayerInfo
	Close()
}

// PlayerInfo is what the admin page shows of a player.
type PlayerInfo struct {
	ID     int    `json:"id"`
	NetID  uint32 `json:"netId"`
	Name   string `json:"name"`
	Remote string `json:"remote"`
	Team   int    `json:"team"`
	Score  int    `json:"score"`
	Ping   int    `json:"ping"`
}

// Session is one game server.
type Session struct {
	ID       string
	Created  time.Time
	Net      *bbnet.Server
	settings Settings
	game     Game
	calls    chan func(Game)
	log      *slog.Logger
	done     chan struct{}
}

// New starts a session around a game.
func New(ctx context.Context, id string, s Settings, net *bbnet.Server, g Game, log *slog.Logger) *Session {
	ses := &Session{
		ID:       id,
		Created:  time.Now(),
		Net:      net,
		settings: s,
		game:     g,
		calls:    make(chan func(Game), 64),
		log:      log.With("session", id),
		done:     make(chan struct{}),
	}
	go ses.run(ctx)
	return ses
}

// Settings returns the session's settings.
func (s *Session) Settings() Settings { return s.settings }

// Done is closed when the session has stopped.
func (s *Session) Done() <-chan struct{} { return s.done }

func (s *Session) run(ctx context.Context) {
	defer close(s.done)
	defer s.Net.Close()
	defer s.game.Close()
	step := time.Second / TickRate
	tick := time.NewTicker(step)
	defer tick.Stop()
	next := time.Now().Add(step)
	for {
		select {
		case <-ctx.Done():
			return
		case f := <-s.calls:
			f(s.game)
		case now := <-tick.C:
			n := 0
			for !now.Before(next) && n < maxCatchUp {
				s.game.Frame()
				next = next.Add(step)
				n++
			}
			if n == maxCatchUp && !now.Before(next) {
				s.log.Warn("session stalled, frames dropped", "behind", now.Sub(next))
				next = now.Add(step)
			}
		}
	}
}

// Do runs f on the session's goroutine, between frames, and waits for it (the admin page).
func (s *Session) Do(f func(Game)) {
	done := make(chan struct{})
	select {
	case s.calls <- func(g Game) { f(g); close(done) }:
		select {
		case <-done:
		case <-s.done:
		}
	case <-s.done:
	}
}
