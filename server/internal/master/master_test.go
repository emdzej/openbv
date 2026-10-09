package master

import (
	"context"
	"encoding/binary"
	"io"
	"log/slog"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"

	"github.com/emdzej/openbv/server/internal/wire"
)

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

// The row's layout is stBV2row's (design/server.md Appendix A): 134 bytes, padding at 121 and 132.
func TestRowLayout(t *testing.T) {
	r := Row{Map: "CTF-Daivuk", ServerName: "My server", Password: "*", IP: "10.1.2.3", Port: 3333,
		Players: 5, MaxPlayers: 16, GameType: 2, Version: "2.11", DBVersion: 8}
	b := r.Encode()
	if len(b) != RowSize {
		t.Fatalf("size %d", len(b))
	}
	if cstring(b[0:17]) != "CTF-Daivuk" || cstring(b[17:81]) != "My server" || cstring(b[81:97]) != "*" ||
		cstring(b[97:113]) != "10.1.2.3" || binary.LittleEndian.Uint16(b[114:]) != 3333 || b[116] != 5 || b[117] != 16 ||
		b[120] != 2 || cstring(b[124:129]) != "2.11" || binary.LittleEndian.Uint16(b[130:]) != 8 || b[121] != 0 || b[132] != 0 {
		t.Fatalf("% x", b)
	}
	back, ok := DecodeRow(b)
	if !ok || back != r {
		t.Fatalf("round trip %+v", back)
	}
	// long names are cut with their NUL kept
	long := Row{ServerName: strings.Repeat("x", 100), Map: strings.Repeat("m", 30)}.Encode()
	if b := long; b[80] != 0 || b[16] != 0 {
		t.Fatal("no terminator")
	}
}

type conn struct {
	t  *testing.T
	ws *websocket.Conn
}

func dial(t *testing.T, url string) *conn {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		t.Fatal(err)
	}
	return &conn{t, ws}
}

func (c *conn) send(typ uint16, data []byte) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := c.ws.Write(ctx, websocket.MessageBinary, wire.Encode(wire.Packet{Type: typ, Data: data})); err != nil {
		c.t.Fatal(err)
	}
}

func (c *conn) read() wire.Packet {
	c.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_, b, err := c.ws.Read(ctx)
	if err != nil {
		c.t.Fatal(err)
	}
	p, _ := wire.Decode(b)
	return p
}

func version(v string) []byte { b := make([]byte, 5); copy(b, v); return b }

// The client's flow (CMaster::requestGames): BV2_LIST, then MASTER_INFO and the rows; a wrong version
// gets -1. Registered servers are listed after the local sessions, with their connection's address.
func TestListAndRegister(t *testing.T) {
	m := New(func() []Row {
		return []Row{{Map: "DM-Arena", ServerName: "local", Port: 3333, MaxPlayers: 16, Version: Version}}
	}, true, quiet)
	srv := httptest.NewServer(m)
	defer srv.Close()
	url := "ws" + strings.TrimPrefix(srv.URL, "http")

	// another server registers itself (its ip empty: the master takes the connection's)
	reg := dial(t, url)
	r := Row{Map: "CTF-Daivuk", ServerName: "remote", Password: "secret", Port: 4444, Players: 2, MaxPlayers: 8}
	reg.send(BV2Row, r.Encode())

	var info wire.Packet
	var rows []Row
	for try := 0; try < 50; try++ { // the registration arrives on its own connection's goroutine
		c := dial(t, url)
		c.send(BV2List, version("2.11"))
		info = c.read()
		n := int(int16(binary.LittleEndian.Uint16(info.Data)))
		rows = rows[:0]
		for i := 0; i < n; i++ {
			p := c.read()
			row, _ := DecodeRow(p.Data)
			rows = append(rows, row)
		}
		c.ws.Close(websocket.StatusNormalClosure, "")
		if len(rows) == 2 {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if info.Type != MasterInfo || len(rows) != 2 {
		t.Fatalf("info %d, rows %+v", info.Type, rows)
	}
	if rows[0].ServerName != "local" || rows[1].ServerName != "remote" || rows[1].IP != "127.0.0.1" || rows[1].Password != "*" {
		t.Fatalf("rows %+v", rows)
	}

	// the server stops: KILL_SERV
	port := make([]byte, 2)
	binary.LittleEndian.PutUint16(port, 4444)
	reg.send(KillServ, port)
	for try := 0; try < 50 && len(m.Rows()) != 1; try++ {
		time.Sleep(20 * time.Millisecond)
	}
	if len(m.Rows()) != 1 {
		t.Fatal("KILL_SERV didn't remove the server")
	}

	old := dial(t, url)
	old.send(BV2List, version("2.10"))
	if p := old.read(); p.Type != MasterInfo || int16(binary.LittleEndian.Uint16(p.Data)) != -1 {
		t.Fatalf("outdated version: %+v", p)
	}
}

func TestRegistrationsExpire(t *testing.T) {
	m := New(nil, true, quiet)
	now := time.Now()
	m.now = func() time.Time { return now }
	m.regs["1.2.3.4:3333"] = registered{row: Row{ServerName: "x"}, seen: now}
	if len(m.Rows()) != 1 {
		t.Fatal("not listed")
	}
	now = now.Add(Expiry + time.Second)
	if len(m.Rows()) != 0 {
		t.Fatal("not expired")
	}
}
