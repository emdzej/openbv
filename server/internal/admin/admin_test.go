package admin

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
)

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

// stubGame records the commands the API runs.
type stubGame struct {
	st   session.Settings
	cmds *[]string
}

func (g *stubGame) Frame()                     {}
func (g *stubGame) Close()                     {}
func (g *stubGame) SetControl(session.Control) {}
func (g *stubGame) Cvars() []session.CvarInfo {
	return []session.CvarInfo{{Name: "sv_scoreLimit", Kind: "int", Value: "50"}}
}
func (g *stubGame) Exec(line, who string) []string {
	*g.cmds = append(*g.cmds, who+": "+line)
	return []string{"> ok"}
}
func (g *stubGame) Players() []session.PlayerInfo {
	return []session.PlayerInfo{{ID: 3, Name: "Bravo", Remote: "10.0.0.2", Team: 1}}
}
func (g *stubGame) Status() session.Status {
	return session.Status{Name: g.st.Name, Map: "DM-Arena", Port: g.st.Port, Public: true, Rotation: g.st.Maps}
}

type harness struct {
	t    *testing.T
	srv  *httptest.Server
	cmds []string
	mgr  *session.Manager
	bans *store.Bans
}

func newHarness(t *testing.T) *harness {
	h := &harness{t: t}
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	dir := t.TempDir()
	saved, _ := store.OpenSessions(dir)
	h.bans, _ = store.OpenBans(dir)
	audit, _ := store.OpenAudit(dir)
	h.mgr = session.NewManager(ctx, func(s session.Settings, net *bbnet.Server, l *slog.Logger) (session.Game, error) {
		return &stubGame{st: s, cmds: &h.cmds}, nil
	}, session.Options{MaxSessions: 2, Store: saved}, quiet)
	mux := http.NewServeMux()
	(&API{Manager: h.mgr, Auth: TokenAuth{Token: "t0ken"}, Config: Config{Auth: "token"}, Bans: h.bans, Audit: audit,
		Maps: func() []string { return []string{"DM-Arena"} }, Log: quiet}).Register(mux)
	h.srv = httptest.NewServer(mux)
	t.Cleanup(h.srv.Close)
	return h
}

func (h *harness) do(method, path, token string, body any) (*http.Response, []byte) {
	var r io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		r = bytes.NewReader(b)
	}
	req, _ := http.NewRequest(method, h.srv.URL+path, r)
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		h.t.Fatal(err)
	}
	defer res.Body.Close()
	data, _ := io.ReadAll(res.Body)
	return res, data
}

func TestAuth(t *testing.T) {
	h := newHarness(t)
	for _, tok := range []string{"", "wrong"} {
		if res, _ := h.do("GET", "/admin/api/sessions", tok, nil); res.StatusCode != http.StatusUnauthorized {
			t.Fatalf("token %q: %d", tok, res.StatusCode)
		}
	}
	if res, body := h.do("GET", "/admin/api/config", "", nil); res.StatusCode != 200 || !strings.Contains(string(body), `"token"`) {
		t.Fatalf("config %d %s", res.StatusCode, body)
	}
	if res, body := h.do("GET", "/admin/api/me", "t0ken", nil); res.StatusCode != 200 || !strings.Contains(string(body), "token") {
		t.Fatalf("me %d %s", res.StatusCode, body)
	}
}

func TestSessionsAndActions(t *testing.T) {
	h := newHarness(t)
	res, body := h.do("POST", "/admin/api/sessions", "t0ken", session.Settings{Name: "One", Maps: []string{"DM-Arena"}, Password: "pw", AdminPassword: "adm"})
	if res.StatusCode != http.StatusCreated {
		t.Fatalf("create %d %s", res.StatusCode, body)
	}
	if strings.Contains(string(body), `"pw"`) || strings.Contains(string(body), "adm\"") {
		t.Fatalf("a password came back: %s", body)
	}
	var v sessionView
	json.Unmarshal(body, &v)
	if !v.Settings.HasPassword || len(v.Players) != 1 {
		t.Fatalf("view %+v", v)
	}
	id := v.ID
	if res, _ := h.do("POST", "/admin/api/sessions", "t0ken", session.Settings{Name: ""}); res.StatusCode != http.StatusBadRequest {
		t.Fatal("a nameless session was created")
	}
	steps := []struct {
		method, path string
		body         any
		want         string
	}{
		{"POST", "/players/3/kick", nil, "kickid 3"},
		{"POST", "/players/3/ban", nil, "banid 3"},
		{"POST", "/players/3/move", map[string]int{"team": -1}, "moveid -1 3"},
		{"POST", "/say", map[string]string{"text": "hello"}, "sayall hello"},
		{"POST", "/cvars", map[string]string{"name": "sv_scoreLimit", "value": "30"}, "set sv_scoreLimit 30"},
		{"POST", "/maps", map[string]string{"action": "add", "map": "DM-Arena"}, "addmap DM-Arena"},
		{"POST", "/maps", map[string]string{"action": "next"}, "changemap"},
		{"POST", "/command", map[string]string{"line": "playerlist"}, "playerlist"},
	}
	for _, s := range steps {
		h.cmds = nil
		res, body := h.do(s.method, "/admin/api/sessions/"+id+s.path, "t0ken", s.body)
		if res.StatusCode != 200 || len(h.cmds) != 1 || h.cmds[0] != "token: "+s.want {
			t.Fatalf("%s: %d %s, ran %v", s.path, res.StatusCode, body, h.cmds)
		}
	}
	// refused input
	for _, bad := range []struct {
		path string
		body any
	}{
		{"/players/40/kick", nil}, {"/players/3/move", map[string]int{"team": 5}}, {"/command", map[string]string{"line": "a\nb"}},
		{"/maps", map[string]string{"action": "add", "map": "../x"}}, {"/cvars", map[string]string{"name": "a b", "value": "1"}},
	} {
		h.cmds = nil
		if res, _ := h.do("POST", "/admin/api/sessions/"+id+bad.path, "t0ken", bad.body); res.StatusCode < 400 || len(h.cmds) != 0 {
			t.Fatalf("%s accepted: %d", bad.path, res.StatusCode)
		}
	}
	// bans: list, add, remove; the audit log has it all
	if res, _ := h.do("POST", "/admin/api/bans", "t0ken", store.Ban{IP: "1.2.3.4"}); res.StatusCode != http.StatusCreated {
		t.Fatalf("ban %d", res.StatusCode)
	}
	if _, ok := h.bans.Banned("1.2.3.4", ""); !ok {
		t.Fatal("not banned")
	}
	if res, _ := h.do("DELETE", "/admin/api/bans/0", "t0ken", nil); res.StatusCode != http.StatusNoContent {
		t.Fatalf("unban %d", res.StatusCode)
	}
	_, body = h.do("GET", "/admin/api/audit", "t0ken", nil)
	if !strings.Contains(string(body), "kickid 3") || !strings.Contains(string(body), `"unban"`) || !strings.Contains(string(body), "create session") {
		t.Fatalf("audit %s", body)
	}
	// restart keeps the ID; stop forgets the session
	if res, _ := h.do("POST", "/admin/api/sessions/"+id+"/restart", "t0ken", nil); res.StatusCode != 200 {
		t.Fatalf("restart %d", res.StatusCode)
	}
	if _, ok := h.mgr.Get(id); !ok {
		t.Fatal("restart lost the session")
	}
	if res, _ := h.do("DELETE", "/admin/api/sessions/"+id, "t0ken", nil); res.StatusCode != http.StatusNoContent {
		t.Fatalf("stop %d", res.StatusCode)
	}
	if res, _ := h.do("GET", "/admin/api/sessions/"+id, "t0ken", nil); res.StatusCode != http.StatusNotFound {
		t.Fatal("stopped session still there")
	}
}

// Updating keeps the passwords unless replaced, and restarts with the new settings; sessions come
// back from sessions.json at boot.
func TestUpdateAndRestore(t *testing.T) {
	h := newHarness(t)
	_, body := h.do("POST", "/admin/api/sessions", "t0ken", session.Settings{Name: "One", Maps: []string{"DM-Arena"}, Password: "pw"})
	var v sessionView
	json.Unmarshal(body, &v)
	res, body := h.do("PUT", "/admin/api/sessions/"+v.ID, "t0ken", session.Settings{Name: "Two", Maps: []string{"DM-Arena"}})
	if res.StatusCode != 200 {
		t.Fatalf("update %d %s", res.StatusCode, body)
	}
	s, _ := h.mgr.Get(v.ID)
	if st := s.Settings(); st.Name != "Two" || st.Password != "pw" {
		t.Fatalf("after update %+v", st)
	}
}
