package game

import (
	"context"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/transport"
	"github.com/emdzej/openbv/server/internal/wire"
)

// the vanilla content: OPENBV_TEST_CONTENT (CI: tools/fetch-content.sh's .deps/content), else ref/
var content = func() string {
	if c := os.Getenv("OPENBV_TEST_CONTENT"); c != "" {
		return c
	}
	return "../../../ref/BaboViolent2/BaboViolent2/Content"
}()

func needContent(t *testing.T) {
	if _, err := os.Stat(content + "/main/maps"); err != nil {
		t.Skip("no vanilla data in ref/ (./play fetches it)")
	}
}

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

func TestInterpolateCubic(t *testing.T) {
	from := CoordFrame{Position: bvmath.Vec3{1, 1, .25}, FrameID: 10}
	to := CoordFrame{Position: bvmath.Vec3{2, 1, .25}, FrameID: 14}
	var cf CoordFrame
	var prog int32
	for i := 0; i < 4; i++ {
		cf.interpolate(&prog, &from, &to, delay, true)
	}
	if cf.Position != to.Position {
		t.Fatalf("at the end of the curve: %v", cf.Position)
	}
	// past the newest frame: extrapolated with the velocity until frame 15, then pinned
	cf.Vel = bvmath.Vec3{3, 0, 0}
	cf.interpolate(&prog, &from, &to, delay, true)
	if want := to.Position[0] + 3*delay; cf.Position[0] != want {
		t.Fatalf("extrapolation: %v, want %v", cf.Position[0], want)
	}
	for prog < 15 {
		cf.interpolate(&prog, &from, &to, delay, true)
	}
	if cf.Position != to.Position {
		t.Fatalf("pinned: %v", cf.Position)
	}
}

// The ping log samples every second frame (PlayerUpdate.cpp:29: 1/30 - 1/30 is not < 0).
func TestPingLogEverySecondFrame(t *testing.T) {
	p := newPlayer(0, 1, 5)
	p.Ping = 60
	for i := 0; i < 2; i++ {
		p.updatePing(delay)
	}
	if p.pingLogID != 1 {
		t.Fatalf("after 2 frames: %d samples", p.pingLogID)
	}
	for i := 0; i < 118; i++ {
		p.updatePing(delay)
	}
	if p.pingLogID != 60 || p.AvgPing != 59 { // the ring subtracts the next slot: 59 of 60 add up
		t.Fatalf("after 120 frames: %d samples, avg %d", p.pingLogID, p.AvgPing)
	}
}

func newTestServer(t *testing.T, gameType int, mapName string) *Server {
	needContent(t)
	s, err := New(session.Settings{Name: "test", GameType: gameType, Maps: []string{mapName}}, content, bbnet.New(), quiet)
	if err != nil {
		t.Fatal(err)
	}
	return s
}

// DM: the spawn farthest from the nearest other living player; alone, a random one.
func TestDMSpawnChoice(t *testing.T) {
	s := newTestServer(t, proto.GameTypeDM, "DM-MiniArena")
	s.players[0] = newPlayer(0, 1, 5)
	s.players[1] = newPlayer(1, 2, 5)
	s.players[0].TeamID, s.players[1].TeamID = proto.TeamBlue, proto.TeamRed
	if !s.spawnPlayer(0) {
		t.Fatal("no spawn")
	}
	first := s.players[0].CurrentCF.Position
	if !s.spawnPlayer(1) {
		t.Fatal("no spawn")
	}
	second := s.players[1].CurrentCF.Position
	best := float32(0)
	for _, sp := range s.m.DMSpawns {
		if d := bvmath.DistanceSquared(sp, first); d > best {
			best = d
		}
	}
	if got := bvmath.DistanceSquared(bvmath.Vec3{second[0], second[1], 0}, bvmath.Vec3{first[0], first[1], 0}); got < best-0.01 {
		t.Fatalf("second spawn %v is not the farthest from %v", second, first)
	}
	if second[2] != .25 {
		t.Fatalf("z = %v", second[2])
	}
}

// --- the protocol, end to end over a WebSocket

type client struct {
	t  *testing.T
	ws *websocket.Conn
}

func dial(t *testing.T, url string) *client {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		t.Fatal(err)
	}
	return &client{t, ws}
}

func (c *client) send(typ uint16, msg any) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := c.ws.Write(ctx, websocket.MessageBinary, wire.Encode(wire.Packet{Type: typ, Data: proto.Encode(msg)})); err != nil {
		c.t.Fatal(err)
	}
}

// expect reads until a message of that type, skipping others, and fails after 5 s.
func (c *client) expect(typ uint16) wire.Packet {
	c.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	for {
		_, b, err := c.ws.Read(ctx)
		if err != nil {
			c.t.Fatalf("waiting for %d: %v", typ, err)
		}
		if p, _ := wire.Decode(b); p.Type == typ {
			return p
		}
	}
}

func startSession(t *testing.T, st session.Settings) (string, func()) {
	needContent(t)
	ctx, cancel := context.WithCancel(context.Background())
	mgr := session.NewManager(ctx, func(s session.Settings, net *bbnet.Server, l *slog.Logger) (session.Game, error) {
		return New(s, content, net, l)
	}, nil, 4, quiet)
	s, err := mgr.Create(st)
	if err != nil {
		t.Fatal(err)
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		transport.Accept(w, r, s.Net, nil, quiet)
	}))
	return "ws" + strings.TrimPrefix(srv.URL, "http") + "/", func() { srv.Close(); cancel() }
}

// join runs the handshake (design/server.md §2.2) up to the end of the state dump.
func join(t *testing.T, c *client, name, password string, others int) (id int8, netID int32) {
	t.Helper()
	var np proto.SvclNewPlayerMsg
	proto.Decode(c.expect(proto.SvclNewPlayer).Data, &np)
	var gv proto.SvclGameVersionMsg
	proto.Decode(c.expect(proto.SvclGameVersion).Data, &gv)
	if gv.GameVersion != proto.GameVersion {
		t.Fatalf("version %d", gv.GameVersion)
	}
	info := proto.PlayerInfoMsg{PlayerID: np.NewPlayerID}
	proto.SetCString(info.PlayerName[:], name)
	c.send(proto.ClsvSvclPlayerInfo, &info)
	pw := proto.ClsvGameVersionAcceptedMsg{PlayerID: np.NewPlayerID}
	proto.SetCString(pw.Password[:], password)
	c.send(proto.ClsvGameVersionAccepted, &pw)
	var echo proto.PlayerInfoMsg
	proto.Decode(c.expect(proto.ClsvSvclPlayerInfo).Data, &echo)
	if proto.CString(echo.PlayerName[:]) != name || proto.CString(echo.PlayerIP[:]) == "" {
		t.Fatalf("player info echo %q %q", echo.PlayerName, echo.PlayerIP)
	}
	c.expect(proto.SvclHashSeed)
	c.send(proto.SvclHashSeedReply, &proto.HashSeedMsg{})
	var si proto.SvclServerInfoMsg
	proto.Decode(c.expect(proto.SvclServerInfo).Data, &si)
	if proto.CString(si.MapName[:]) != "DM-MiniArena" || si.GameType != proto.GameTypeDM {
		t.Fatalf("server info %q %d", si.MapName, si.GameType)
	}
	c.expect(proto.SvclGameState)
	for i := 0; i < 72; i++ {
		var sc proto.SvclSvChangeMsg
		proto.Decode(c.expect(proto.SvclSvChange).Data, &sc)
		if text := proto.CString(sc.SvChange[:]); strings.HasPrefix(text, "set sv_password") && text != `set sv_password ""` {
			t.Fatalf("password sent: %q", text)
		}
	}
	for i := 0; i < others; i++ {
		c.expect(proto.SvclPlayerEnumState)
	}
	return np.NewPlayerID, np.BaboNetID
}

func TestWrongPasswordIsRefused(t *testing.T) {
	url, stop := startSession(t, session.Settings{Name: "test", GameType: 0, Maps: []string{"DM-MiniArena"}, Password: "Secret"})
	defer stop()
	c := dial(t, url)
	var np proto.SvclNewPlayerMsg
	proto.Decode(c.expect(proto.SvclNewPlayer).Data, &np)
	c.expect(proto.SvclGameVersion)
	pw := proto.ClsvGameVersionAcceptedMsg{PlayerID: np.NewPlayerID}
	proto.SetCString(pw.Password[:], "nope")
	c.send(proto.ClsvGameVersionAccepted, &pw)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	for {
		_, b, err := c.ws.Read(ctx)
		if err != nil {
			if ctx.Err() != nil {
				t.Fatal("still connected")
			}
			return // closed by the server
		}
		if p, _ := wire.Decode(b); p.Type == proto.SvclServerInfo {
			t.Fatal("got the state dump with a wrong password")
		}
	}
}

func TestTwoPlayersSeeEachOther(t *testing.T) {
	url, stop := startSession(t, session.Settings{Name: "test", GameType: 0, Maps: []string{"DM-MiniArena"}, Password: "Secret"})
	defer stop()

	a := dial(t, url)
	ida, neta := join(t, a, "Alice", "secret", 0) // case-insensitive, as CString's != was
	b := dial(t, url)
	idb, _ := join(t, b, "Bob", "SECRET", 1)
	if ida == idb {
		t.Fatal("same slot")
	}

	// teams: auto-assign puts them on opposite teams, echoed to everyone
	a.send(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: ida, TeamRequested: proto.TeamAutoAssign})
	b.send(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: idb, TeamRequested: proto.TeamAutoAssign})
	teams := map[int8]int8{}
	for i := 0; i < 2; i++ {
		var tr proto.TeamRequestMsg
		proto.Decode(b.expect(proto.ClsvSvclTeamRequest).Data, &tr)
		teams[tr.PlayerID] = tr.TeamRequested
	}
	if teams[ida] == teams[idb] || teams[ida] < 0 || teams[idb] < 0 {
		t.Fatalf("teams %v", teams)
	}

	// a request for someone else's player is ignored (BindPlayerID)
	b.send(proto.ClsvSpawnRequest, &proto.ClsvSpawnRequestMsg{PlayerID: ida, WeaponID: proto.WeaponSMG, MeleeID: proto.WeaponKnives})
	// spawning: everyone gets PLAYER_SPAWN, the position ×10
	a.send(proto.ClsvSpawnRequest, &proto.ClsvSpawnRequestMsg{PlayerID: ida, WeaponID: proto.WeaponSMG, MeleeID: proto.WeaponKnives})
	var sp proto.SvclPlayerSpawnMsg
	proto.Decode(b.expect(proto.SvclPlayerSpawn).Data, &sp)
	if sp.PlayerID != ida || sp.Position[2] != 2 { // z .25 ×10, truncated
		t.Fatalf("spawn %+v", sp)
	}

	// Alice moves: Bob gets her coord frames, interpolated on the server
	for f := int32(1); f <= 20; f++ {
		a.send(proto.ClsvSvclPlayerCoordFrame, &proto.PlayerCoordFrameMsg{
			PlayerID: ida, FrameID: f * 2, BaboNetID: neta,
			Position: [3]int16{sp.Position[0]*10 + int16(f), sp.Position[1] * 10, 25},
		})
	}
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		var cf proto.PlayerCoordFrameMsg
		proto.Decode(b.expect(proto.ClsvSvclPlayerCoordFrame).Data, &cf)
		if cf.PlayerID == ida && cf.Position[0] > sp.Position[0]*10 {
			return // she moved
		}
	}
	t.Fatal("Bob never saw Alice move")
}
