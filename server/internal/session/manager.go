package session

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"sort"
	"sync"
	"time"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/store"
	"github.com/emdzej/openbv/server/internal/transport"
)

// NewGame makes the rules for a new session around its connections.
type NewGame func(s Settings, net *bbnet.Server, log *slog.Logger) (Game, error)

// Options configure a manager.
type Options struct {
	Origins     []string        // browser origins allowed to connect (the web player)
	MaxSessions int             // sessions at most
	Store       *store.Sessions // where sessions are saved (nil: nowhere)
	// ConnPerMinute bounds new game connections per client address and minute (0: no limit).
	ConnPerMinute int
}

// Manager owns the sessions of one server process.
type Manager struct {
	ctx     context.Context
	newGame NewGame
	log     *slog.Logger
	opts    Options
	limiter *limiter

	mu       sync.Mutex
	sessions map[string]*entry
}

type entry struct {
	s        *Session
	cancel   context.CancelFunc
	listener *http.Server
	restart  bool // stopped to be started again: keep it saved
}

var (
	ErrTooMany  = errors.New("too many sessions")
	ErrNotFound = errors.New("no such session")
	ErrPortUsed = errors.New("port in use by another session")
)

// NewManager makes a manager; sessions stop when ctx does.
func NewManager(ctx context.Context, newGame NewGame, opts Options, log *slog.Logger) *Manager {
	if opts.MaxSessions <= 0 {
		opts.MaxSessions = 16
	}
	m := &Manager{ctx: ctx, newGame: newGame, log: log, opts: opts, sessions: map[string]*entry{}}
	if opts.ConnPerMinute > 0 {
		m.limiter = newLimiter(opts.ConnPerMinute, time.Minute)
	}
	return m
}

// Create starts a new session and saves it. With Settings.Port set it also listens on that port, at
// "/", which is where the game's own client connects (ws://<host>:<port>/,
// src/port/babonet_gasm.cpp).
func (m *Manager) Create(st Settings) (*Session, error) {
	s, err := m.start(newID(), st)
	if err != nil {
		return nil, err
	}
	m.save(s.ID, st)
	return s, nil
}

// Restore starts the saved sessions (at boot). A session that fails to start is logged and kept
// saved, so a fixed configuration brings it back.
func (m *Manager) Restore() {
	if m.opts.Store == nil {
		return
	}
	for _, sv := range m.opts.Store.List() {
		var st Settings
		if err := json.Unmarshal(sv.Settings, &st); err != nil {
			m.log.Error("a saved session can't be read", "session", sv.ID, "err", err)
			continue
		}
		if _, err := m.start(sv.ID, st); err != nil {
			m.log.Error("a saved session can't start", "session", sv.ID, "err", err)
		}
	}
}

func (m *Manager) start(id string, st Settings) (*Session, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.sessions) >= m.opts.MaxSessions {
		return nil, ErrTooMany
	}
	if _, ok := m.sessions[id]; ok {
		return nil, fmt.Errorf("session %s is running", id)
	}
	for _, e := range m.sessions {
		if st.Port != 0 && e.s.Settings().Port == st.Port {
			return nil, ErrPortUsed
		}
	}
	ring := newLogRing(1000)
	log := slog.New(&teeHandler{next: m.log.Handler(), ring: ring}).With("session", id)
	bnet := bbnet.New()
	g, err := m.newGame(st, bnet, log)
	if err != nil {
		return nil, err
	}
	var ln net.Listener
	if st.Port != 0 {
		ln, err = net.Listen("tcp", fmt.Sprintf(":%d", st.Port))
		if err != nil {
			g.Close()
			return nil, fmt.Errorf("listen on %d: %w", st.Port, err)
		}
	}
	ctx, cancel := context.WithCancel(m.ctx)
	s := New(ctx, id, st, bnet, g, log)
	s.Log = ring
	e := &entry{s: s, cancel: cancel}
	if ln != nil {
		mux := http.NewServeMux()
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { m.serve(s, w, r) })
		e.listener = &http.Server{Handler: mux, ReadHeaderTimeout: 10 * time.Second}
		go e.listener.Serve(ln)
	}
	m.sessions[id] = e
	// the game's hooks: called on the session's goroutine, so they never wait for it
	g.SetControl(Control{
		Restart: func() { go m.Restart(id) },
		Stop:    func() { go m.Stop(id) },
		Changed: func(c Changes) { m.changed(id, c) },
	})
	go func() {
		<-s.Done()
		if e.listener != nil {
			e.listener.Close()
		}
		m.mu.Lock()
		if m.sessions[id] == e {
			delete(m.sessions, id)
		}
		m.mu.Unlock()
	}()
	log.Info("session started", "name", st.Name, "port", st.Port, "gameType", st.GameType)
	return s, nil
}

// Stop ends a session and forgets it; its players are disconnected.
func (m *Manager) Stop(id string) error {
	if err := m.halt(id); err != nil {
		return err
	}
	if m.opts.Store != nil {
		if err := m.opts.Store.Delete(id); err != nil {
			m.log.Error("saving sessions", "err", err)
		}
	}
	return nil
}

// Restart stops a session and starts it again with its settings (and the same ID and port).
func (m *Manager) Restart(id string) (*Session, error) {
	m.mu.Lock()
	e, ok := m.sessions[id]
	m.mu.Unlock()
	if !ok {
		return nil, ErrNotFound
	}
	st := e.s.Settings()
	if err := m.halt(id); err != nil {
		return nil, err
	}
	return m.start(id, st)
}

// Update replaces a session's settings and restarts it with them.
func (m *Manager) Update(id string, st Settings) (*Session, error) {
	m.mu.Lock()
	_, ok := m.sessions[id]
	m.mu.Unlock()
	if !ok {
		return nil, ErrNotFound
	}
	if err := m.halt(id); err != nil {
		return nil, err
	}
	s, err := m.start(id, st)
	if err != nil {
		return nil, err
	}
	m.save(id, st)
	return s, nil
}

func (m *Manager) halt(id string) error {
	m.mu.Lock()
	e, ok := m.sessions[id]
	m.mu.Unlock()
	if !ok {
		return ErrNotFound
	}
	e.cancel()
	<-e.s.Done()
	if e.listener != nil {
		e.listener.Close() // free the port before a restart takes it again
	}
	m.mu.Lock()
	if m.sessions[id] == e {
		delete(m.sessions, id)
	}
	m.mu.Unlock()
	return nil
}

// changed folds what a game's console changed into its saved settings.
func (m *Manager) changed(id string, c Changes) {
	m.mu.Lock()
	e, ok := m.sessions[id]
	m.mu.Unlock()
	if !ok {
		return
	}
	st := e.s.setChanges(c)
	m.save(id, st)
}

func (m *Manager) save(id string, st Settings) {
	if m.opts.Store == nil {
		return
	}
	if err := m.opts.Store.Put(id, st); err != nil {
		m.log.Error("saving sessions", "err", err)
	}
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

// ServePort serves a game connection for the session listening on port: /bv2/{port}, the form a
// client can build from a game list row (port) and a proxy's address.
func (m *Manager) ServePort(port int, w http.ResponseWriter, r *http.Request) {
	for _, s := range m.List() {
		if s.Settings().Port == port {
			m.serve(s, w, r)
			return
		}
	}
	http.NotFound(w, r)
}

func (m *Manager) serve(s *Session, w http.ResponseWriter, r *http.Request) {
	if m.limiter != nil && !m.limiter.allow(transport.RemoteIP(r)) {
		http.Error(w, "too many connections", http.StatusTooManyRequests)
		return
	}
	transport.Accept(w, r, s.Net, m.opts.Origins, m.log)
}

func newID() string {
	b := make([]byte, 4)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// limiter allows at most n events per key in a sliding window (as ../nowhereinparticular's).
type limiter struct {
	n      int
	window time.Duration
	mu     sync.Mutex
	hits   map[string][]time.Time
}

func newLimiter(n int, window time.Duration) *limiter {
	return &limiter{n: n, window: window, hits: map[string][]time.Time{}}
}

func (l *limiter) allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	cut := now.Add(-l.window)
	h := l.hits[key]
	i := 0
	for i < len(h) && h[i].Before(cut) {
		i++
	}
	h = h[i:]
	if len(h) >= l.n {
		l.hits[key] = h
		return false
	}
	l.hits[key] = append(h, now)
	if len(l.hits) > 10000 {
		for k, v := range l.hits {
			if len(v) == 0 || v[len(v)-1].Before(cut) {
				delete(l.hits, k)
			}
		}
	}
	return true
}
