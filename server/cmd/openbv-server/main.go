// openbv-server: Babo Violent 2 game servers for the openbv client, several sessions per process,
// with an admin page and the game list (master) for the game's Game Browser.
//
// Configuration (environment):
//
//	OPENBV_LISTEN         the HTTP address: the admin page, the shared game endpoints /bv2/{session}
//	                      and /bv2/port/{port}, the master at /master (:8080)
//	OPENBV_CONTENT        the game's content folder (bv2.db, main/): maps are read from main/maps (content)
//	OPENBV_DATA           where the server keeps sessions.json, bans.json and audit.log ("": in memory)
//	OPENBV_ADMIN_TOKEN    the admin page's token (a fixed bearer token; with OIDC, for scripts)
//	OIDC_ISSUER           an OpenID Connect provider for the admin page (e.g. a Keycloak realm URL)
//	OIDC_CLIENT_ID        the page's public client there
//	OIDC_ROLE             the role an admin needs (a realm role, a client role or a flat "roles" claim)
//	OPENBV_MASTER_PORT    the master's own port, as the game's master=host:port names it (10207; 0: none,
//	                      only /master on OPENBV_LISTEN)
//	OPENBV_MASTER_REGISTER  "1": other servers may list themselves with this master
//	OPENBV_ORIGINS        browser origins allowed to connect (the web player), comma-separated
//	OPENBV_MAX_SESSIONS   sessions at most (16)
//	OPENBV_CONN_PER_MINUTE  new game connections per client address and minute (30; 0: no limit)
//	OPENBV_TRUST_PROXY    "1": take the client's address from X-Forwarded-For (behind a proxy only)
//	OPENBV_DEBUG          1: debug logs, 2: also every packet sent and received
//	OPENBV_SESSION        a session to start when none is saved, as JSON (session.Settings), e.g.
//	                      {"name":"My server","gameType":0,"port":3333,"maps":["CTF-Daivuk"]}
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/emdzej/openbv/server/internal/admin"
	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/bvmap"
	"github.com/emdzej/openbv/server/internal/game"
	"github.com/emdzej/openbv/server/internal/master"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
	"github.com/emdzej/openbv/server/internal/transport"
)

var version = "dev" // -ldflags "-X main.version=..."

func env(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func main() {
	log := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelInfo}))
	switch os.Getenv("OPENBV_DEBUG") {
	case "":
	case "2": // every packet too
		log = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelDebug - 4}))
	default:
		log = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelDebug}))
	}
	if err := run(log); err != nil {
		log.Error("openbv-server", "err", err)
		os.Exit(1)
	}
}

func run(log *slog.Logger) error {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	var origins []string
	if o := os.Getenv("OPENBV_ORIGINS"); o != "" {
		origins = strings.Split(o, ",")
	}
	maxSessions, _ := strconv.Atoi(env("OPENBV_MAX_SESSIONS", "16"))
	connPerMinute, _ := strconv.Atoi(env("OPENBV_CONN_PER_MINUTE", "30"))
	transport.TrustProxy = os.Getenv("OPENBV_TRUST_PROXY") == "1"
	content := env("OPENBV_CONTENT", "content")
	maps := bvmap.Library{Dir: filepath.Join(content, "main", "maps")}
	if len(maps.List()) == 0 {
		log.Warn("no maps in the content folder: sessions can't start", "dir", maps.Dir)
	}

	data := os.Getenv("OPENBV_DATA")
	if data != "" {
		if err := os.MkdirAll(data, 0o700); err != nil {
			return err
		}
	}
	bans, err := store.OpenBans(data)
	if err != nil {
		return fmt.Errorf("bans.json: %w", err)
	}
	saved, err := store.OpenSessions(data)
	if err != nil {
		return fmt.Errorf("sessions.json: %w", err)
	}
	audit, err := store.OpenAudit(data)
	if err != nil {
		return fmt.Errorf("audit.log: %w", err)
	}

	newGame := func(s session.Settings, net *bbnet.Server, l *slog.Logger) (session.Game, error) {
		return game.New(s, content, net, l, bans)
	}
	mgr := session.NewManager(ctx, newGame, session.Options{
		Origins: origins, MaxSessions: maxSessions, Store: saved, ConnPerMinute: connPerMinute,
	}, log)
	mgr.Restore()
	if js := os.Getenv("OPENBV_SESSION"); js != "" && len(saved.List()) == 0 {
		var st session.Settings
		if err := json.Unmarshal([]byte(js), &st); err != nil {
			return fmt.Errorf("OPENBV_SESSION: %w", err)
		}
		if _, err := mgr.Create(st); err != nil {
			return fmt.Errorf("starting the boot session: %w", err)
		}
	}

	// the master: this process's public sessions (their game list rows), and registered servers
	ms := master.New(func() []master.Row { return localRows(mgr) }, os.Getenv("OPENBV_MASTER_REGISTER") == "1", log.With("component", "master"))

	// the admin page's sign-in: OIDC for people, the token for scripts
	var auth admin.AnyAuth
	cfg := admin.Config{Auth: "token"}
	if iss := os.Getenv("OIDC_ISSUER"); iss != "" {
		auth = append(auth, &admin.OIDCAuth{Issuer: iss, ClientID: os.Getenv("OIDC_CLIENT_ID"), Role: os.Getenv("OIDC_ROLE")})
		cfg = admin.Config{Auth: "oidc", Issuer: iss, ClientID: os.Getenv("OIDC_CLIENT_ID")}
	}
	if tok := os.Getenv("OPENBV_ADMIN_TOKEN"); tok != "" {
		auth = append(auth, admin.TokenAuth{Token: tok})
	}
	if len(auth) == 0 {
		log.Warn("no OPENBV_ADMIN_TOKEN or OIDC_ISSUER: the admin page can't be used")
	}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) { w.Write([]byte("ok\n")) })
	mux.HandleFunc("GET /bv2/{id}", func(w http.ResponseWriter, r *http.Request) { mgr.ServeSession(r.PathValue("id"), w, r) })
	mux.HandleFunc("GET /bv2/port/{port}", func(w http.ResponseWriter, r *http.Request) {
		port, err := strconv.Atoi(r.PathValue("port"))
		if err != nil {
			http.NotFound(w, r)
			return
		}
		mgr.ServePort(port, w, r)
	})
	mux.Handle("GET /master", ms)
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) { http.Redirect(w, r, "/admin/", http.StatusFound) })
	(&admin.API{Manager: mgr, Auth: auth, Config: cfg, Bans: bans, Audit: audit, Maps: maps.List, Log: log}).Register(mux)

	srv := &http.Server{Addr: env("OPENBV_LISTEN", ":8080"), Handler: mux, ReadHeaderTimeout: 10 * time.Second}
	servers := []*http.Server{srv}
	if mp, _ := strconv.Atoi(env("OPENBV_MASTER_PORT", "10207")); mp != 0 {
		ln, err := net.Listen("tcp", fmt.Sprintf(":%d", mp))
		if err != nil {
			return fmt.Errorf("the master's port %d: %w", mp, err)
		}
		msrv := &http.Server{Handler: ms, ReadHeaderTimeout: 10 * time.Second}
		servers = append(servers, msrv)
		go msrv.Serve(ln)
		log.Info("master", "port", mp)
	}
	go func() {
		<-ctx.Done()
		sctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		for _, s := range servers {
			s.Shutdown(sctx)
		}
	}()
	log.Info("openbv-server", "version", version, "listen", srv.Addr, "content", content, "data", data)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return nil
}

// localRows are the game list rows of the public sessions: the ip left empty, so the client joins
// them at the master's host (CMaster::gameHost), and a placeholder for a password (never the password:
// the original sent it to every browsing client, design/server.md §9.1).
func localRows(mgr *session.Manager) []master.Row {
	var rows []master.Row
	for _, s := range mgr.List() {
		var st session.Status
		s.Do(func(g session.Game) { st = g.Status() })
		if !st.Public || st.Port == 0 {
			continue
		}
		pw := ""
		if st.Passworded {
			pw = "*"
		}
		rows = append(rows, master.Row{
			Map: st.Map, ServerName: st.Name, Password: pw, Port: uint16(st.Port),
			Players: st.Players, MaxPlayers: st.MaxPlayers, GameType: st.GameType,
			Version: master.Version, DBVersion: 8,
		})
	}
	return rows
}
