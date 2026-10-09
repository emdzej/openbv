// Package store keeps what a server process remembers between runs, as files in its data folder
// (OPENBV_DATA): the ban list every session shares (bans.json), the sessions to start again at boot
// (sessions.json) and the admin actions (audit.log, one JSON object per line). Without a folder
// everything stays in memory.
package store

import (
	"encoding/json"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// Ban is one entry of the ban list. The original banned by IP only (main/banlist: name + IP); a MAC
// (the client's player info, a stable random one per openbv installation) is banned too when known.
type Ban struct {
	Name string    `json:"name"`
	IP   string    `json:"ip,omitempty"`
	MAC  string    `json:"mac,omitempty"`
	At   time.Time `json:"at"`
	By   string    `json:"by,omitempty"`
}

// Bans is the ban list, safe for every session's goroutine.
type Bans struct {
	path string // "" in memory
	mu   sync.Mutex
	list []Ban
}

// OpenBans reads dir/bans.json (dir "" or a missing file: an empty list).
func OpenBans(dir string) (*Bans, error) {
	b := &Bans{}
	if dir == "" {
		return b, nil
	}
	b.path = filepath.Join(dir, "bans.json")
	if err := readJSON(b.path, &b.list); err != nil {
		return nil, err
	}
	return b, nil
}

// List is a copy of the list, in the order bans were added (unban takes an index into it).
func (b *Bans) List() []Ban {
	b.mu.Lock()
	defer b.mu.Unlock()
	return append([]Ban(nil), b.list...)
}

// Add appends a ban and saves the list.
func (b *Bans) Add(ban Ban) error {
	b.mu.Lock()
	defer b.mu.Unlock()
	if ban.At.IsZero() {
		ban.At = time.Now().UTC()
	}
	b.list = append(b.list, ban)
	return b.save()
}

// Remove deletes entry i; false if there's none.
func (b *Bans) Remove(i int) (Ban, bool, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if i < 0 || i >= len(b.list) {
		return Ban{}, false, nil
	}
	ban := b.list[i]
	b.list = append(b.list[:i], b.list[i+1:]...)
	return ban, true, b.save()
}

// Banned reports the first ban matching the address or the MAC (either may be empty).
func (b *Bans) Banned(ip, mac string) (Ban, bool) {
	b.mu.Lock()
	defer b.mu.Unlock()
	for _, ban := range b.list {
		if ip != "" && ban.IP == ip {
			return ban, true
		}
		if mac != "" && ban.MAC != "" && strings.EqualFold(ban.MAC, mac) {
			return ban, true
		}
	}
	return Ban{}, false
}

func (b *Bans) save() error {
	if b.path == "" {
		return nil
	}
	return writeJSON(b.path, b.list)
}

// Saved is one session in sessions.json: its ID (kept across restarts) and settings, as JSON the
// session package defines.
type Saved struct {
	ID       string          `json:"id"`
	Settings json.RawMessage `json:"settings"`
}

// Sessions is sessions.json.
type Sessions struct {
	path string
	mu   sync.Mutex
	list []Saved
}

// OpenSessions reads dir/sessions.json.
func OpenSessions(dir string) (*Sessions, error) {
	s := &Sessions{}
	if dir == "" {
		return s, nil
	}
	s.path = filepath.Join(dir, "sessions.json")
	if err := readJSON(s.path, &s.list); err != nil {
		return nil, err
	}
	return s, nil
}

// List is the saved sessions, in the order they were created.
func (s *Sessions) List() []Saved {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]Saved(nil), s.list...)
}

// Put adds or replaces a session.
func (s *Sessions) Put(id string, settings any) error {
	raw, err := json.Marshal(settings)
	if err != nil {
		return err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	for i := range s.list {
		if s.list[i].ID == id {
			s.list[i].Settings = raw
			return s.save()
		}
	}
	s.list = append(s.list, Saved{ID: id, Settings: raw})
	return s.save()
}

// Delete forgets a session.
func (s *Sessions) Delete(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i := range s.list {
		if s.list[i].ID == id {
			s.list = append(s.list[:i], s.list[i+1:]...)
			return s.save()
		}
	}
	return nil
}

func (s *Sessions) save() error {
	if s.path == "" {
		return nil
	}
	return writeJSON(s.path, s.list)
}

// Entry is one admin action.
type Entry struct {
	At      time.Time `json:"at"`
	Who     string    `json:"who"`
	Action  string    `json:"action"`
	Session string    `json:"session,omitempty"`
	Detail  string    `json:"detail,omitempty"`
}

// Audit is the admin actions: appended to dir/audit.log, the last ones kept in memory for the page.
type Audit struct {
	path   string
	mu     sync.Mutex
	recent []Entry
}

const auditKeep = 500

// OpenAudit opens dir/audit.log and reads its tail.
func OpenAudit(dir string) (*Audit, error) {
	a := &Audit{}
	if dir == "" {
		return a, nil
	}
	a.path = filepath.Join(dir, "audit.log")
	data, err := os.ReadFile(a.path)
	if err != nil && !errors.Is(err, fs.ErrNotExist) {
		return nil, err
	}
	for _, line := range strings.Split(string(data), "\n") {
		var e Entry
		if line != "" && json.Unmarshal([]byte(line), &e) == nil {
			a.recent = append(a.recent, e)
		}
	}
	if len(a.recent) > auditKeep {
		a.recent = a.recent[len(a.recent)-auditKeep:]
	}
	return a, nil
}

// Log records an action.
func (a *Audit) Log(e Entry) error {
	if e.At.IsZero() {
		e.At = time.Now().UTC()
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	a.recent = append(a.recent, e)
	if len(a.recent) > auditKeep {
		a.recent = a.recent[len(a.recent)-auditKeep:]
	}
	if a.path == "" {
		return nil
	}
	line, _ := json.Marshal(e)
	f, err := os.OpenFile(a.path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(append(line, '\n'))
	return err
}

// Recent is the last actions, newest last.
func (a *Audit) Recent() []Entry {
	a.mu.Lock()
	defer a.mu.Unlock()
	return append([]Entry(nil), a.recent...)
}

func readJSON(path string, v any) error {
	data, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	return json.Unmarshal(data, v)
}

// writeJSON replaces path atomically (a temporary file, then a rename).
func writeJSON(path string, v any) error {
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, append(data, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
