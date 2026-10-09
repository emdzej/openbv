// Package master is the game list the client's Game Browser asks for (design/server.md §9; the
// client's side is CMaster.cpp, the structures cMSstruct.h). The original was RndLabs' master server;
// here a server process is the master for its own public sessions, and other openbv servers (or
// original ones speaking the same messages over the WebSocket transport) may register with it.
//
// The flow: the client connects and sends BV2_LIST {version}; the master answers MASTER_INFO
// {number of games} (-1: outdated version), then that many BV2_ROW. A game server reports itself
// with BV2_ROW every 20 s (its ip empty: the master takes the connection's address) and KILL_SERV
// {port} when it stops.
package master

import (
	"encoding/binary"
	"log/slog"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/emdzej/openbv/server/internal/transport"
	"github.com/emdzej/openbv/server/internal/wire"
)

// Message IDs (cMSstruct.h).
const (
	BV2Row     = 997
	BV2List    = 999
	KillServ   = 1001
	MasterInfo = 1002
)

// Version is the game version the list is for (bv2.db LauncherSettings.Version).
const Version = "2.11"

// RowSize is sizeof(stBV2row).
const RowSize = 134

// Row is a game in the list (stBV2row).
type Row struct {
	Map        string
	ServerName string
	Password   string // only whether it is empty matters to the client; never a real password here
	IP         string // empty: the client uses the master's address (openbv; CMaster.cpp)
	Port       uint16
	Players    int
	MaxPlayers int
	Flags      uint16
	GameType   int
	ServerID   uint16
	Version    string
	Priority   int8
	DBVersion  uint16
}

func putString(b []byte, s string) {
	n := copy(b[:len(b)-1], s)
	for i := n; i < len(b); i++ {
		b[i] = 0
	}
}

func cstring(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}

// Encode is the row as the client's memcpy reads it (MSVC x86 layout: padding after gameType and at
// the end, design/server.md Appendix A).
func (r Row) Encode() []byte {
	b := make([]byte, RowSize)
	putString(b[0:17], r.Map)
	putString(b[17:81], r.ServerName)
	putString(b[81:97], r.Password)
	putString(b[97:113], r.IP)
	binary.LittleEndian.PutUint16(b[114:], r.Port)
	b[116] = byte(int8(r.Players))
	b[117] = byte(int8(r.MaxPlayers))
	binary.LittleEndian.PutUint16(b[118:], r.Flags)
	b[120] = byte(int8(r.GameType))
	binary.LittleEndian.PutUint16(b[122:], r.ServerID)
	putString(b[124:129], r.Version)
	b[129] = byte(r.Priority)
	binary.LittleEndian.PutUint16(b[130:], r.DBVersion)
	return b
}

// DecodeRow reads a row a game server sent.
func DecodeRow(b []byte) (Row, bool) {
	if len(b) < RowSize {
		return Row{}, false
	}
	return Row{
		Map: cstring(b[0:17]), ServerName: cstring(b[17:81]), Password: cstring(b[81:97]), IP: cstring(b[97:113]),
		Port: binary.LittleEndian.Uint16(b[114:]), Players: int(int8(b[116])), MaxPlayers: int(int8(b[117])),
		Flags: binary.LittleEndian.Uint16(b[118:]), GameType: int(int8(b[120])), ServerID: binary.LittleEndian.Uint16(b[122:]),
		Version: cstring(b[124:129]), Priority: int8(b[129]), DBVersion: binary.LittleEndian.Uint16(b[130:]),
	}, true
}

// Expiry is how long a registered server stays listed without an update (they report every 20 s).
const Expiry = 60 * time.Second

type registered struct {
	row  Row
	seen time.Time
}

// Master answers game-list requests.
type Master struct {
	// Local lists this process's public sessions.
	Local func() []Row
	// AcceptRegistrations lets other servers list themselves (BV2_ROW from a game server).
	AcceptRegistrations bool
	Log                 *slog.Logger
	now                 func() time.Time

	mu   sync.Mutex
	regs map[string]registered // by address:port
}

// New makes a master over the local list.
func New(local func() []Row, acceptRegistrations bool, log *slog.Logger) *Master {
	return &Master{Local: local, AcceptRegistrations: acceptRegistrations, Log: log, now: time.Now, regs: map[string]registered{}}
}

// Rows is the list a client gets: the local sessions, then the registered servers.
func (m *Master) Rows() []Row {
	var rows []Row
	if m.Local != nil {
		rows = append(rows, m.Local()...)
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	keys := make([]string, 0, len(m.regs))
	now := m.now()
	for k, r := range m.regs {
		if now.Sub(r.seen) > Expiry {
			delete(m.regs, k)
			continue
		}
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		rows = append(rows, m.regs[k].row)
	}
	return rows
}

// ServeHTTP serves the master over a WebSocket (the client connects to ws://<master>/).
func (m *Master) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	transport.Accept(w, r, m, nil, m.Log)
}

// --- transport.Handler

func (m *Master) Connected(c *transport.Conn) {}

func (m *Master) Disconnected(c *transport.Conn, err error) {}

func (m *Master) Received(c *transport.Conn, p wire.Packet) {
	switch p.Type {
	case BV2List: // CMaster::requestGames (CMaster.cpp:904)
		version := cstring(p.Data)
		if version != Version {
			c.Send(wire.Packet{Type: MasterInfo, Data: masterInfo(-1)})
			return
		}
		rows := m.Rows()
		if len(rows) > 32767 {
			rows = rows[:32767]
		}
		c.Send(wire.Packet{Type: MasterInfo, Data: masterInfo(int16(len(rows)))})
		for _, r := range rows {
			c.Send(wire.Packet{Type: BV2Row, Data: r.Encode()})
		}
	case BV2Row: // a game server's report (Server.cpp:578)
		if !m.AcceptRegistrations {
			return
		}
		row, ok := DecodeRow(p.Data)
		if !ok || row.Port == 0 {
			return
		}
		row.IP = c.Remote
		if len(row.IP) > 15 || strings.Contains(row.IP, ":") {
			return // an IPv6 address doesn't fit the client's 16-byte field
		}
		if row.Password != "" {
			row.Password = "*"
		}
		row.Version = Version
		key := c.Remote + ":" + strconv.Itoa(int(row.Port))
		m.mu.Lock()
		if _, ok := m.regs[key]; !ok && len(m.regs) >= 1024 {
			m.mu.Unlock()
			return
		}
		m.regs[key] = registered{row: row, seen: m.now()}
		m.mu.Unlock()
	case KillServ:
		if len(p.Data) < 2 {
			return
		}
		key := c.Remote + ":" + strconv.Itoa(int(binary.LittleEndian.Uint16(p.Data)))
		m.mu.Lock()
		delete(m.regs, key)
		m.mu.Unlock()
	}
}

func masterInfo(n int16) []byte {
	b := make([]byte, 2)
	binary.LittleEndian.PutUint16(b, uint16(n))
	return b
}
