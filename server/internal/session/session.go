// Package session runs game sessions: each one is a Babo Violent 2 server of its own (its port, its
// settings, its players) with a single goroutine that owns all its state and steps the game at the
// original's fixed 30 Hz (dkcInit(30)), catching up after a stall as the original's main loop did.
// Connections only queue packets (internal/bbnet); the game reads them during its frame.
package session

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/emdzej/openbv/server/internal/bbnet"
)

// TickRate is the original's fixed update rate.
const TickRate = 30

// maxCatchUp bounds the frames run back to back after a stall.
const maxCatchUp = 10

// Settings are a session's configuration: what the admin page sets when creating it, kept in
// OPENBV_DATA/sessions.json and updated when the console changes them (Changes).
type Settings struct {
	Name       string   `json:"name"`     // sv_gameName
	Password   string   `json:"password"` // sv_password
	GameType   int      `json:"gameType"` // sv_gameType
	MaxPlayers int      `json:"maxPlayers"`
	Maps       []string `json:"maps"` // the rotation, by name without .bvm
	Port       int      `json:"port"` // sv_port: the session listens there

	// Cvars are any other sv_* (and zsv_*) values, as the console's `set` takes them; applied after
	// the fields above, so they win.
	Cvars map[string]string `json:"cvars,omitempty"`
	// AdminUser and AdminPassword are zsv_adminUser and zsv_adminPass: the in-game admin login
	// ("admin <user> <password>" in the client's console). Both empty: no in-game admins.
	AdminUser     string `json:"adminUser,omitempty"`
	AdminPassword string `json:"adminPassword,omitempty"`
	// VoteOn are the commands players may vote on (the console's voteon), e.g. "kick", "changemap",
	// "sv_gametype". Empty: no vote is valid, as in the original.
	VoteOn []string `json:"voteOn,omitempty"`
	// Private keeps the session out of the master's list (sv_gamePublic false).
	Private bool `json:"private,omitempty"`
}

// Changes are what a game's console changed: the sv_*/zsv_* values set since it started, the
// rotation and the votable commands.
type Changes struct {
	Cvars  map[string]string
	Maps   []string
	VoteOn []string
}

// Control is what a game may ask of its session. Its functions are called from the session's
// goroutine; they must not wait for it.
type Control struct {
	Restart func()        // the console's restart: the session starts again with its settings
	Stop    func()        // the console's quit
	Changed func(Changes) // save what the console changed
}

// Game is the rules side of a session (internal/game). All calls come from the session's goroutine.
type Game interface {
	Frame()
	Players() []PlayerInfo
	Status() Status
	Cvars() []CvarInfo
	// Exec runs a console command as the given admin and returns what it printed.
	Exec(line, who string) []string
	SetControl(Control)
	Close()
}

// PlayerInfo is what the admin page shows of a player.
type PlayerInfo struct {
	ID     int    `json:"id"`
	NetID  uint32 `json:"netId"`
	Name   string `json:"name"`
	Remote string `json:"remote"`
	MAC    string `json:"mac,omitempty"`
	Team   int    `json:"team"`
	Score  int    `json:"score"`
	Kills  int    `json:"kills"`
	Deaths int    `json:"deaths"`
	Ping   int    `json:"ping"` // ms
	Admin  bool   `json:"admin,omitempty"`
}

// Status is a session's game as the admin page and the master see it.
type Status struct {
	Name          string      `json:"name"`
	Map           string      `json:"map"`
	NextMap       string      `json:"nextMap"`
	GameType      int         `json:"gameType"`
	RoundState    int         `json:"roundState"`
	Players       int         `json:"players"`
	MaxPlayers    int         `json:"maxPlayers"`
	Port          int         `json:"port"`
	Passworded    bool        `json:"passworded"`
	Public        bool        `json:"public"`
	BlueScore     int         `json:"blueScore"`
	RedScore      int         `json:"redScore"`
	BlueWin       int         `json:"blueWin"`
	RedWin        int         `json:"redWin"`
	GameTimeLeft  float32     `json:"gameTimeLeft"`
	RoundTimeLeft float32     `json:"roundTimeLeft"`
	Rotation      []string    `json:"rotation"`
	VoteOn        []string    `json:"voteOn"`
	Vote          *VoteStatus `json:"vote,omitempty"`
}

// VoteStatus is a vote in progress.
type VoteStatus struct {
	From      string  `json:"from"`
	What      string  `json:"what"`
	Yes       int     `json:"yes"`
	No        int     `json:"no"`
	Voters    int     `json:"voters"`
	Remaining float32 `json:"remaining"`
}

// CvarInfo is one console variable.
type CvarInfo struct {
	Name  string `json:"name"`
	Help  string `json:"help"`
	Kind  string `json:"kind"` // bool, int, float, string
	Value string `json:"value"`
}

// Session is one game server.
type Session struct {
	ID      string
	Created time.Time
	Net     *bbnet.Server
	Log     *LogRing // its last log lines (the admin page)

	mu       sync.Mutex
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
		log:      log,
		done:     make(chan struct{}),
	}
	go ses.run(ctx)
	return ses
}

// Settings returns the session's settings (with what its console changed since).
func (s *Session) Settings() Settings {
	s.mu.Lock()
	defer s.mu.Unlock()
	st := s.settings
	st.Maps = append([]string(nil), st.Maps...)
	st.VoteOn = append([]string(nil), st.VoteOn...)
	if st.Cvars != nil {
		c := make(map[string]string, len(st.Cvars))
		for k, v := range st.Cvars {
			c[k] = v
		}
		st.Cvars = c
	}
	return st
}

// setChanges folds a console's changes into the settings and returns them.
func (s *Session) setChanges(c Changes) Settings {
	s.mu.Lock()
	if len(c.Cvars) > 0 && s.settings.Cvars == nil {
		s.settings.Cvars = map[string]string{}
	}
	for k, v := range c.Cvars {
		s.settings.Cvars[k] = v
	}
	if c.Maps != nil {
		s.settings.Maps = c.Maps
	}
	if c.VoteOn != nil {
		s.settings.VoteOn = c.VoteOn
	}
	s.mu.Unlock()
	return s.Settings()
}

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
