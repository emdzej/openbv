// Package admin is the server's admin API and page: sessions (create, stop, inspect) and their
// players. Requests carry the admin token as a bearer token; the page asks for it once and keeps it
// in the browser's session storage.
package admin

import (
	"crypto/subtle"
	"embed"
	"encoding/json"
	"errors"
	"io/fs"
	"log/slog"
	"net/http"
	"strings"
	"time"

	"github.com/emdzej/openbv/server/internal/session"
)

//go:embed static
var static embed.FS

// API serves /admin/ (the page) and /admin/api/.
type API struct {
	Manager *session.Manager
	Token   string
	Log     *slog.Logger
}

// Register adds the routes to mux.
func (a *API) Register(mux *http.ServeMux) {
	page, _ := fs.Sub(static, "static")
	mux.Handle("GET /admin/", http.StripPrefix("/admin/", http.FileServerFS(page)))
	mux.HandleFunc("GET /admin/api/sessions", a.auth(a.listSessions))
	mux.HandleFunc("POST /admin/api/sessions", a.auth(a.createSession))
	mux.HandleFunc("GET /admin/api/sessions/{id}", a.auth(a.getSession))
	mux.HandleFunc("DELETE /admin/api/sessions/{id}", a.auth(a.stopSession))
}

func (a *API) auth(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		tok, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
		if a.Token == "" || !ok || subtle.ConstantTimeCompare([]byte(tok), []byte(a.Token)) != 1 {
			writeError(w, http.StatusUnauthorized, "unauthorized")
			return
		}
		h(w, r)
	}
}

type sessionView struct {
	ID       string               `json:"id"`
	Created  time.Time            `json:"created"`
	Settings session.Settings     `json:"settings"`
	Players  []session.PlayerInfo `json:"players"`
}

func view(s *session.Session) sessionView {
	v := sessionView{ID: s.ID, Created: s.Created, Settings: s.Settings()}
	s.Do(func(g session.Game) { v.Players = g.Players() })
	if v.Players == nil {
		v.Players = []session.PlayerInfo{}
	}
	v.Settings.Password = "" // never sent back
	return v
}

func (a *API) listSessions(w http.ResponseWriter, r *http.Request) {
	out := []sessionView{}
	for _, s := range a.Manager.List() {
		out = append(out, view(s))
	}
	writeJSON(w, http.StatusOK, out)
}

func (a *API) createSession(w http.ResponseWriter, r *http.Request) {
	var st session.Settings
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<16)).Decode(&st); err != nil {
		writeError(w, http.StatusBadRequest, "bad JSON: "+err.Error())
		return
	}
	s, err := a.Manager.Create(st)
	switch {
	case errors.Is(err, session.ErrTooMany), errors.Is(err, session.ErrPortUsed):
		writeError(w, http.StatusConflict, err.Error())
		return
	case err != nil:
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	a.Log.Info("admin: session created", "id", s.ID, "name", st.Name)
	writeJSON(w, http.StatusCreated, view(s))
}

func (a *API) getSession(w http.ResponseWriter, r *http.Request) {
	s, ok := a.Manager.Get(r.PathValue("id"))
	if !ok {
		writeError(w, http.StatusNotFound, "no such session")
		return
	}
	writeJSON(w, http.StatusOK, view(s))
}

func (a *API) stopSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if err := a.Manager.Stop(id); err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	a.Log.Info("admin: session stopped", "id", id)
	w.WriteHeader(http.StatusNoContent)
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, map[string]string{"error": msg})
}
