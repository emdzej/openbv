// openbv-server: Babo Violent 2 game servers for the openbv client, several sessions per process,
// with an admin page.
//
// Configuration (environment):
//
//	OPENBV_LISTEN       the HTTP address for the admin page and the shared game endpoint /bv2/{session} (:8080)
//	OPENBV_ADMIN_TOKEN  the admin page's token (required to use it)
//	OPENBV_CONTENT      the game's content folder (bv2.db, main/): maps are read from main/maps
//	OPENBV_ORIGINS      browser origins allowed to connect (the web player), comma-separated
//	OPENBV_MAX_SESSIONS sessions at most (16)
//	OPENBV_SESSION      a session to start at boot, as JSON (session.Settings), e.g.
//	                    {"name":"My server","gameType":0,"port":3333,"maps":["CTF-Daivuk"]}
package main

import (
	"context"
	"encoding/json"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/emdzej/openbv/server/internal/admin"
	"github.com/emdzej/openbv/server/internal/game"
	"github.com/emdzej/openbv/server/internal/session"
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
	if os.Getenv("OPENBV_DEBUG") != "" {
		log = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelDebug}))
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	var origins []string
	if o := os.Getenv("OPENBV_ORIGINS"); o != "" {
		origins = strings.Split(o, ",")
	}
	maxSessions, _ := strconv.Atoi(env("OPENBV_MAX_SESSIONS", "16"))
	content := env("OPENBV_CONTENT", "content")

	newGame := func(s session.Settings, l *slog.Logger) (session.Game, error) {
		return game.New(s, content, l)
	}
	mgr := session.NewManager(ctx, newGame, origins, maxSessions, log)

	if js := os.Getenv("OPENBV_SESSION"); js != "" {
		var st session.Settings
		if err := json.Unmarshal([]byte(js), &st); err != nil {
			log.Error("OPENBV_SESSION", "err", err)
			os.Exit(2)
		}
		if _, err := mgr.Create(st); err != nil {
			log.Error("starting the boot session", "err", err)
			os.Exit(1)
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) { w.Write([]byte("ok\n")) })
	mux.HandleFunc("GET /bv2/{id}", func(w http.ResponseWriter, r *http.Request) { mgr.ServeSession(r.PathValue("id"), w, r) })
	(&admin.API{Manager: mgr, Token: os.Getenv("OPENBV_ADMIN_TOKEN"), Log: log}).Register(mux)

	srv := &http.Server{Addr: env("OPENBV_LISTEN", ":8080"), Handler: mux, ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		sctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		srv.Shutdown(sctx)
	}()
	log.Info("openbv-server", "version", version, "listen", srv.Addr, "content", content)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Error("listen", "err", err)
		os.Exit(1)
	}
}
