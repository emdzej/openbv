// Package session runs game sessions: each one is a Babo Violent 2 server of its own (its port, its
// settings, its players) with a single goroutine that owns all its state and steps the game at the
// original's 30 Hz (dkcInit(30)). Connections only post events to it.
package session

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/emdzej/openbv/server/internal/transport"
	"github.com/emdzej/openbv/server/internal/wire"
)

// TickRate is the original's fixed update rate.
const TickRate = 30

// Settings are a session's sv_* values that the admin page sets when creating it.
type Settings struct {
	Name       string   `json:"name"`     // sv_gameName
	Password   string   `json:"password"` // sv_password
	GameType   int      `json:"gameType"` // sv_gameType
	MaxPlayers int      `json:"maxPlayers"`
	Maps       []string `json:"maps"` // the rotation, by name without .bvm
	Port       int      `json:"port"` // sv_port: the session also listens there
}

// Game is the rules side of a session (internal/game). All calls come from the session's goroutine.
type Game interface {
	Join(c *transport.Conn)
	Packet(c *transport.Conn, p wire.Packet)
	Leave(c *transport.Conn)
	Tick(dt float64)
	Players() []PlayerInfo
	Close()
}

// PlayerInfo is what the admin page shows of a player.
type PlayerInfo struct {
	NetID  uint32 `json:"netId"`
	Name   string `json:"name"`
	Remote string `json:"remote"`
	Team   int    `json:"team"`
	Score  int    `json:"score"`
	Ping   int    `json:"ping"`
}

type eventKind int

const (
	evJoin eventKind = iota
	evPacket
	evLeave
	evCall
)

type event struct {
	kind eventKind
	conn *transport.Conn
	pkt  wire.Packet
	call func(Game)
}

// Session is one game server.
type Session struct {
	ID       string
	Created  time.Time
	settings Settings
	game     Game
	events   chan event
	log      *slog.Logger

	mu    sync.Mutex
	conns map[uint32]*transport.Conn
	done  chan struct{}
}

// New starts a session around a game.
func New(ctx context.Context, id string, s Settings, g Game, log *slog.Logger) *Session {
	ses := &Session{
		ID:       id,
		Created:  time.Now(),
		settings: s,
		game:     g,
		events:   make(chan event, 4096),
		log:      log.With("session", id),
		conns:    map[uint32]*transport.Conn{},
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
	defer s.game.Close()
	tick := time.NewTicker(time.Second / TickRate)
	defer tick.Stop()
	last := time.Now()
	for {
		select {
		case <-ctx.Done():
			s.mu.Lock()
			for _, c := range s.conns {
				c.Close("server stopped")
			}
			s.mu.Unlock()
			return
		case e := <-s.events:
			switch e.kind {
			case evJoin:
				s.game.Join(e.conn)
			case evPacket:
				s.game.Packet(e.conn, e.pkt)
			case evLeave:
				s.game.Leave(e.conn)
			case evCall:
				e.call(s.game)
			}
		case now := <-tick.C:
			dt := now.Sub(last).Seconds()
			last = now
			s.game.Tick(dt)
		}
	}
}

// Do runs f on the session's goroutine and waits for it (the admin page's reads and actions).
func (s *Session) Do(f func(Game)) {
	done := make(chan struct{})
	select {
	case s.events <- event{kind: evCall, call: func(g Game) { f(g); close(done) }}:
		select {
		case <-done:
		case <-s.done:
		}
	case <-s.done:
	}
}

// The transport.Handler side: connections post events.

func (s *Session) Connected(c *transport.Conn) {
	s.mu.Lock()
	s.conns[c.ID] = c
	s.mu.Unlock()
	s.post(event{kind: evJoin, conn: c})
}

func (s *Session) Received(c *transport.Conn, p wire.Packet) {
	// the payload aliases the reader's buffer: keep a copy
	p.Data = append([]byte(nil), p.Data...)
	s.post(event{kind: evPacket, conn: c, pkt: p})
}

func (s *Session) Disconnected(c *transport.Conn, err error) {
	s.mu.Lock()
	delete(s.conns, c.ID)
	s.mu.Unlock()
	s.post(event{kind: evLeave, conn: c})
}

func (s *Session) post(e event) {
	if e.kind != evPacket {
		// joins and leaves always get through: the game must see every client go
		select {
		case s.events <- e:
		case <-s.done:
		}
		return
	}
	select {
	case s.events <- e:
	case <-s.done:
	default:
		// the game can't keep up with this client's packets: drop the client rather than the session
		e.conn.Close("server busy")
	}
}
