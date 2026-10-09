package session

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"sort"
	"sync"

	"github.com/emdzej/openbv/server/internal/transport"
)

// NewGame makes the rules for a new session.
type NewGame func(s Settings, log *slog.Logger) (Game, error)

// Manager owns the sessions of one server process.
type Manager struct {
	ctx         context.Context
	newGame     NewGame
	log         *slog.Logger
	origins     []string
	maxSessions int

	mu       sync.Mutex
	sessions map[string]*entry
}

type entry struct {
	s        *Session
	cancel   context.CancelFunc
	listener *http.Server
}

var (
	ErrTooMany  = errors.New("too many sessions")
	ErrNotFound = errors.New("no such session")
	ErrPortUsed = errors.New("port in use by another session")
)

// NewManager makes a manager; sessions stop when ctx does.
func NewManager(ctx context.Context, newGame NewGame, origins []string, maxSessions int, log *slog.Logger) *Manager {
	return &Manager{ctx: ctx, newGame: newGame, log: log, origins: origins, maxSessions: maxSessions, sessions: map[string]*entry{}}
}

// Create starts a session. With Settings.Port set it also listens on that port, at "/", which is
// where the game's own client connects (ws://<host>:<port>/, src/port/babonet_gasm.cpp).
func (m *Manager) Create(st Settings) (*Session, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.sessions) >= m.maxSessions {
		return nil, ErrTooMany
	}
	for _, e := range m.sessions {
		if st.Port != 0 && e.s.settings.Port == st.Port {
			return nil, ErrPortUsed
		}
	}
	id := newID()
	log := m.log.With("session", id)
	g, err := m.newGame(st, log)
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithCancel(m.ctx)
	s := New(ctx, id, st, g, m.log)
	e := &entry{s: s, cancel: cancel}
	if st.Port != 0 {
		ln, err := net.Listen("tcp", fmt.Sprintf(":%d", st.Port))
		if err != nil {
			cancel()
			return nil, fmt.Errorf("listen on %d: %w", st.Port, err)
		}
		mux := http.NewServeMux()
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { m.serve(s, w, r) })
		e.listener = &http.Server{Handler: mux}
		go e.listener.Serve(ln)
	}
	m.sessions[id] = e
	go func() {
		<-s.Done()
		m.mu.Lock()
		if m.sessions[id] == e {
			delete(m.sessions, id)
		}
		m.mu.Unlock()
		if e.listener != nil {
			e.listener.Close()
		}
	}()
	log.Info("session started", "name", st.Name, "port", st.Port, "gameType", st.GameType)
	return s, nil
}

// Stop ends a session; its players are disconnected.
func (m *Manager) Stop(id string) error {
	m.mu.Lock()
	e, ok := m.sessions[id]
	m.mu.Unlock()
	if !ok {
		return ErrNotFound
	}
	e.cancel()
	<-e.s.Done()
	return nil
}

// Get returns a session.
func (m *Manager) Get(id string) (*Session, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	e, ok := m.sessions[id]
	if !ok {
		return nil, false
	}
	return e.s, true
}

// List returns the sessions, oldest first.
func (m *Manager) List() []*Session {
	m.mu.Lock()
	out := make([]*Session, 0, len(m.sessions))
	for _, e := range m.sessions {
		out = append(out, e.s)
	}
	m.mu.Unlock()
	sort.Slice(out, func(i, j int) bool { return out[i].Created.Before(out[j].Created) })
	return out
}

// ServeSession serves a game connection for session id: the shared endpoint /bv2/{id}, for
// deployments behind a single reverse-proxied port (the client takes a ws:// or wss:// URL as host).
func (m *Manager) ServeSession(id string, w http.ResponseWriter, r *http.Request) {
	s, ok := m.Get(id)
	if !ok {
		http.NotFound(w, r)
		return
	}
	m.serve(s, w, r)
}

func (m *Manager) serve(s *Session, w http.ResponseWriter, r *http.Request) {
	transport.Accept(w, r, s, m.origins, m.log)
}

func newID() string {
	b := make([]byte, 4)
	rand.Read(b)
	return hex.EncodeToString(b)
}
