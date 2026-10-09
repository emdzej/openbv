// Package admin is the server's admin API and page: sessions (create, edit, restart, stop), their
// players (kick, ban, move, say), console commands, variables, the map rotation, the log, the ban list
// and the audit log of admin actions. Requests carry a bearer token: the admin token, or an OIDC
// access token when OIDC_ISSUER is set (the page signs in with authorization code + PKCE).
package admin

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"io/fs"
	"log/slog"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
)

//go:embed static
var static embed.FS

// Config is what the page needs to sign in.
type Config struct {
	Auth     string `json:"auth"` // "token" or "oidc"
	Issuer   string `json:"issuer,omitempty"`
	ClientID string `json:"client_id,omitempty"`
}

// API serves /admin/ (the page) and /admin/api/.
type API struct {
	Manager *session.Manager
	Auth    Auth
	Config  Config
	Bans    *store.Bans
	Audit   *store.Audit
	Maps    func() []string // the maps the server has
	Log     *slog.Logger
}

type ctxKey struct{}

// Register adds the routes to mux.
func (a *API) Register(mux *http.ServeMux) {
	page, _ := fs.Sub(static, "static")
	mux.Handle("GET /admin/", http.StripPrefix("/admin/", http.FileServerFS(page)))
	mux.HandleFunc("GET /admin/api/config", func(w http.ResponseWriter, r *http.Request) { writeJSON(w, http.StatusOK, a.Config) })
	route := func(pattern string, h http.HandlerFunc) { mux.HandleFunc(pattern, a.auth(h)) }
	route("GET /admin/api/me", a.me)
	route("GET /admin/api/sessions", a.listSessions)
	route("POST /admin/api/sessions", a.createSession)
	route("GET /admin/api/sessions/{id}", a.getSession)
	route("PUT /admin/api/sessions/{id}", a.updateSession)
	route("DELETE /admin/api/sessions/{id}", a.stopSession)
	route("POST /admin/api/sessions/{id}/restart", a.restartSession)
	route("POST /admin/api/sessions/{id}/command", a.command)
	route("POST /admin/api/sessions/{id}/say", a.say)
	route("POST /admin/api/sessions/{id}/cvars", a.setCvar)
	route("GET /admin/api/sessions/{id}/cvars", a.cvars)
	route("POST /admin/api/sessions/{id}/maps", a.maps)
	route("POST /admin/api/sessions/{id}/players/{player}/{action}", a.playerAction)
	route("GET /admin/api/sessions/{id}/log", a.logTail)
	route("GET /admin/api/maps", a.availableMaps)
	route("GET /admin/api/bans", a.listBans)
	route("POST /admin/api/bans", a.addBan)
	route("DELETE /admin/api/bans/{index}", a.removeBan)
	route("GET /admin/api/audit", a.audit)
}

func (a *API) auth(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		tok, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
		if !ok || a.Auth == nil {
			writeError(w, http.StatusUnauthorized, "unauthorized")
			return
		}
		who, err := a.Auth.Verify(r.Context(), tok)
		if errors.Is(err, ErrForbidden) {
			writeError(w, http.StatusUnauthorized, "unauthorized")
			return
		}
		if err != nil {
			a.Log.Error("admin sign-in check", "err", err)
			writeError(w, http.StatusServiceUnavailable, "the sign-in provider can't be reached")
			return
		}
		h(w, r.WithContext(context.WithValue(r.Context(), ctxKey{}, who)))
	}
}

func who(r *http.Request) string { s, _ := r.Context().Value(ctxKey{}).(string); return s }

func (a *API) record(r *http.Request, action, sessionID, detail string) {
	a.Log.Info("admin: "+action, "who", who(r), "session", sessionID, "detail", detail)
	if a.Audit == nil {
		return
	}
	if err := a.Audit.Log(store.Entry{Who: who(r), Action: action, Session: sessionID, Detail: detail}); err != nil {
		a.Log.Error("writing the audit log", "err", err)
	}
}

func (a *API) me(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"who": who(r)})
}

// --- sessions

type settingsView struct {
	session.Settings
	HasPassword      bool `json:"hasPassword"`
	HasAdminPassword bool `json:"hasAdminPassword"`
}

type sessionView struct {
	ID       string               `json:"id"`
	Created  time.Time            `json:"created"`
	Settings settingsView         `json:"settings"`
	Status   session.Status       `json:"status"`
	Players  []session.PlayerInfo `json:"players"`
}

func view(s *session.Session) sessionView {
	st := s.Settings()
	v := sessionView{ID: s.ID, Created: s.Created, Settings: settingsView{Settings: st,
		HasPassword: st.Password != "", HasAdminPassword: st.AdminPassword != ""}}
	// never sent back
	v.Settings.Password, v.Settings.AdminPassword = "", ""
	if v.Settings.Cvars != nil {
		delete(v.Settings.Cvars, "sv_password")
		delete(v.Settings.Cvars, "zsv_adminPass")
	}
	s.Do(func(g session.Game) { v.Players = g.Players(); v.Status = g.Status() })
	if v.Players == nil {
		v.Players = []session.PlayerInfo{}
	}
	return v
}

func (a *API) session(w http.ResponseWriter, r *http.Request) (*session.Session, bool) {
	s, ok := a.Manager.Get(r.PathValue("id"))
	if !ok {
		writeError(w, http.StatusNotFound, "no such session")
	}
	return s, ok
}

func (a *API) listSessions(w http.ResponseWriter, r *http.Request) {
	out := []sessionView{}
	for _, s := range a.Manager.List() {
		out = append(out, view(s))
	}
	writeJSON(w, http.StatusOK, out)
}

func decode(w http.ResponseWriter, r *http.Request, v any) bool {
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<16)).Decode(v); err != nil {
		writeError(w, http.StatusBadRequest, "bad JSON: "+err.Error())
		return false
	}
	return true
}

func validSettings(st session.Settings) error {
	if strings.TrimSpace(st.Name) == "" {
		return errors.New("a name is needed")
	}
	if st.GameType < 0 || st.GameType > 3 {
		return errors.New("the game type is 0 to 3")
	}
	if st.MaxPlayers < 0 || st.MaxPlayers > 32 {
		return errors.New("at most 32 players")
	}
	if st.Port != 0 && (st.Port < 1024 || st.Port > 65535) {
		return errors.New("the port is 1024 to 65535")
	}
	if len(st.Password) > 15 {
		return errors.New("the password is at most 15 characters")
	}
	return nil
}

func (a *API) startErr(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, session.ErrTooMany), errors.Is(err, session.ErrPortUsed):
		writeError(w, http.StatusConflict, err.Error())
	case errors.Is(err, session.ErrNotFound):
		writeError(w, http.StatusNotFound, err.Error())
	default:
		writeError(w, http.StatusBadRequest, err.Error())
	}
}

func (a *API) createSession(w http.ResponseWriter, r *http.Request) {
	var st session.Settings
	if !decode(w, r, &st) {
		return
	}
	if err := validSettings(st); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	s, err := a.Manager.Create(st)
	if err != nil {
		a.startErr(w, err)
		return
	}
	a.record(r, "create session", s.ID, st.Name)
	writeJSON(w, http.StatusCreated, view(s))
}

func (a *API) getSession(w http.ResponseWriter, r *http.Request) {
	if s, ok := a.session(w, r); ok {
		writeJSON(w, http.StatusOK, view(s))
	}
}

// updateSession replaces the settings and restarts the session. Passwords left empty keep the
// current ones unless "clearPassword"/"clearAdminPassword" is set.
func (a *API) updateSession(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var body struct {
		session.Settings
		ClearPassword      bool `json:"clearPassword"`
		ClearAdminPassword bool `json:"clearAdminPassword"`
	}
	if !decode(w, r, &body) {
		return
	}
	st := body.Settings
	old := s.Settings()
	if st.Password == "" && !body.ClearPassword {
		st.Password = old.Password
	}
	if st.AdminPassword == "" && !body.ClearAdminPassword {
		st.AdminPassword = old.AdminPassword
	}
	if err := validSettings(st); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	ns, err := a.Manager.Update(s.ID, st)
	if err != nil {
		a.startErr(w, err)
		return
	}
	a.record(r, "update session", s.ID, st.Name)
	writeJSON(w, http.StatusOK, view(ns))
}

func (a *API) stopSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if err := a.Manager.Stop(id); err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	a.record(r, "stop session", id, "")
	w.WriteHeader(http.StatusNoContent)
}

func (a *API) restartSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	s, err := a.Manager.Restart(id)
	if err != nil {
		a.startErr(w, err)
		return
	}
	a.record(r, "restart session", id, "")
	writeJSON(w, http.StatusOK, view(s))
}

// exec runs a console command on a session and answers with what it printed.
func (a *API) exec(w http.ResponseWriter, r *http.Request, s *session.Session, line, action string) {
	var out []string
	s.Do(func(g session.Game) { out = g.Exec(line, who(r)) })
	if out == nil {
		out = []string{}
	}
	a.record(r, action, s.ID, line)
	writeJSON(w, http.StatusOK, map[string]any{"output": out})
}

func (a *API) command(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var body struct {
		Line string `json:"line"`
	}
	if !decode(w, r, &body) {
		return
	}
	line := strings.TrimSpace(body.Line)
	if line == "" || strings.ContainsAny(line, "\r\n") {
		writeError(w, http.StatusBadRequest, "one command, please")
		return
	}
	a.exec(w, r, s, line, "command")
}

func (a *API) say(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var body struct {
		Text   string `json:"text"`
		Player *int   `json:"player"` // a private message to one player
	}
	if !decode(w, r, &body) {
		return
	}
	text := strings.ReplaceAll(strings.TrimSpace(body.Text), "\n", " ")
	if text == "" {
		writeError(w, http.StatusBadRequest, "nothing to say")
		return
	}
	if body.Player != nil {
		a.exec(w, r, s, "sayid "+strconv.Itoa(*body.Player)+" "+text, "say")
		return
	}
	a.exec(w, r, s, "sayall "+text, "say")
}

func (a *API) cvars(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var out []session.CvarInfo
	s.Do(func(g session.Game) { out = g.Cvars() })
	writeJSON(w, http.StatusOK, out)
}

func (a *API) setCvar(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var body struct {
		Name  string `json:"name"`
		Value string `json:"value"`
	}
	if !decode(w, r, &body) {
		return
	}
	if strings.ContainsAny(body.Name, " \r\n") || strings.ContainsAny(body.Value, "\r\n") || body.Name == "" {
		writeError(w, http.StatusBadRequest, "bad variable")
		return
	}
	detail := "set " + body.Name + " " + body.Value
	if strings.EqualFold(body.Name, "sv_password") || strings.EqualFold(body.Name, "zsv_adminPass") {
		detail = "set " + body.Name + " ********"
	}
	var out []string
	s.Do(func(g session.Game) { out = g.Exec("set "+body.Name+" "+body.Value, who(r)) })
	a.record(r, "set variable", s.ID, detail)
	writeJSON(w, http.StatusOK, map[string]any{"output": out})
}

// maps edits the rotation: {"action": "add"|"remove"|"change"|"next", "map": name}.
func (a *API) maps(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	var body struct {
		Action string `json:"action"`
		Map    string `json:"map"`
	}
	if !decode(w, r, &body) {
		return
	}
	if strings.ContainsAny(body.Map, " \r\n/\\") {
		writeError(w, http.StatusBadRequest, "bad map name")
		return
	}
	cmd := map[string]string{"add": "addmap ", "remove": "removemap ", "change": "changemap ", "next": "changemap"}[body.Action]
	if cmd == "" {
		writeError(w, http.StatusBadRequest, "action: add, remove, change or next")
		return
	}
	if body.Action == "next" {
		body.Map = ""
	}
	a.exec(w, r, s, cmd+body.Map, "maps "+body.Action)
}

// playerAction: kick, ban, move (body {"team": -1|0|1}).
func (a *API) playerAction(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	id, err := strconv.Atoi(r.PathValue("player"))
	if err != nil || id < 0 || id >= 32 {
		writeError(w, http.StatusBadRequest, "bad player")
		return
	}
	switch r.PathValue("action") {
	case "kick":
		a.exec(w, r, s, "kickid "+strconv.Itoa(id), "kick")
	case "ban":
		a.exec(w, r, s, "banid "+strconv.Itoa(id), "ban")
	case "move":
		var body struct {
			Team int `json:"team"`
		}
		if !decode(w, r, &body) {
			return
		}
		if body.Team < -1 || body.Team > 1 {
			writeError(w, http.StatusBadRequest, "team: -1 spectator, 0 blue, 1 red")
			return
		}
		a.exec(w, r, s, "moveid "+strconv.Itoa(body.Team)+" "+strconv.Itoa(id), "move")
	default:
		writeError(w, http.StatusNotFound, "kick, ban or move")
	}
}

func (a *API) logTail(w http.ResponseWriter, r *http.Request) {
	s, ok := a.session(w, r)
	if !ok {
		return
	}
	since, _ := strconv.ParseUint(r.URL.Query().Get("since"), 10, 64)
	lines := []session.LogLine{}
	if s.Log != nil {
		lines = s.Log.Since(since)
	}
	writeJSON(w, http.StatusOK, lines)
}

func (a *API) availableMaps(w http.ResponseWriter, r *http.Request) {
	maps := []string{}
	if a.Maps != nil {
		maps = append(maps, a.Maps()...)
	}
	writeJSON(w, http.StatusOK, maps)
}

// --- bans and audit

type banView struct {
	Index int `json:"index"`
	store.Ban
}

func (a *API) listBans(w http.ResponseWriter, r *http.Request) {
	out := []banView{}
	if a.Bans != nil {
		for i, b := range a.Bans.List() {
			out = append(out, banView{Index: i, Ban: b})
		}
	}
	writeJSON(w, http.StatusOK, out)
}

func (a *API) addBan(w http.ResponseWriter, r *http.Request) {
	var b store.Ban
	if !decode(w, r, &b) {
		return
	}
	b.IP, b.MAC, b.Name = strings.TrimSpace(b.IP), strings.TrimSpace(b.MAC), strings.TrimSpace(b.Name)
	if (b.IP == "" && b.MAC == "") || a.Bans == nil {
		writeError(w, http.StatusBadRequest, "an IP or a MAC, please")
		return
	}
	if b.Name == "" {
		b.Name = "MANUAL-BAN"
	}
	b.By, b.At = who(r), time.Time{}
	if err := a.Bans.Add(b); err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	a.record(r, "ban", "", strings.TrimSpace(b.Name+" "+b.IP+" "+b.MAC))
	w.WriteHeader(http.StatusCreated)
}

func (a *API) removeBan(w http.ResponseWriter, r *http.Request) {
	i, err := strconv.Atoi(r.PathValue("index"))
	if err != nil || a.Bans == nil {
		writeError(w, http.StatusBadRequest, "bad index")
		return
	}
	b, ok, err := a.Bans.Remove(i)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err.Error())
		return
	}
	if !ok {
		writeError(w, http.StatusNotFound, "no such ban")
		return
	}
	a.record(r, "unban", "", strings.TrimSpace(b.Name+" "+b.IP+" "+b.MAC))
	w.WriteHeader(http.StatusNoContent)
}

func (a *API) audit(w http.ResponseWriter, r *http.Request) {
	out := []store.Entry{}
	if a.Audit != nil {
		out = a.Audit.Recent()
	}
	writeJSON(w, http.StatusOK, out)
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, map[string]string{"error": msg})
}
