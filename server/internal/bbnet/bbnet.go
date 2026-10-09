// Package bbnet is the server half of baboNet (src/engine/babonet/baboNet.cpp, cServer.cpp) as the
// game code sees it, over openbv's WebSocket transport. The game calls it the way Server.cpp calls
// bb_serverUpdate, bb_serverReceive, bb_serverSend and bb_serverDisconnectClient, and gets the same
// behaviour (design/server.md §3):
//
//   - Update returns one event per call: a lost client, else (at a half-second check) one admitted
//     connection, else nothing. ConnCheck grows by the elapsed time of every call.
//   - NetIDs count from 1 in admission order, per server.
//   - Receive returns packets oldest first, all of the first client's before the second client's
//     (clients in admission order).
//   - Disconnecting a client (the server's own kick) reports no lost event.
//
// Connections arrive and send from their own goroutines; everything the game calls happens on the
// session's goroutine.
package bbnet

import (
	"sync"

	"github.com/emdzej/openbv/server/internal/transport"
	"github.com/emdzej/openbv/server/internal/wire"
)

// Event is what Update reports.
type Event struct {
	Kind  EventKind
	NetID uint32
	IP    string // for New: the client's address (bb_serverUpdate's newIP)
}

type EventKind int

const (
	EvNone EventKind = iota
	EvNew
	EvLost
)

type client struct {
	conn   *transport.Conn
	netID  uint32
	inbox  []wire.Packet
	kicked bool
}

// Server is one game server's connections.
type Server struct {
	mu        sync.Mutex
	pending   []*client
	clients   []*client
	byConn    map[*transport.Conn]*client
	lost      []uint32
	connCheck float32
	lastNetID uint32

	// Trace, when set, sees every packet sent and received (OPENBV_DEBUG).
	Trace func(dir string, netID uint32, p wire.Packet)
}

// New makes a server with no connections.
func New() *Server { return &Server{byConn: map[*transport.Conn]*client{}} }

// --- transport.Handler (connection goroutines)

func (s *Server) Connected(c *transport.Conn) {
	s.mu.Lock()
	defer s.mu.Unlock()
	cl := &client{conn: c}
	s.pending = append(s.pending, cl)
	s.byConn[c] = cl
}

func (s *Server) Received(c *transport.Conn, p wire.Packet) {
	p.Data = append([]byte(nil), p.Data...) // the reader reuses its buffer
	s.mu.Lock()
	defer s.mu.Unlock()
	if cl := s.byConn[c]; cl != nil {
		cl.inbox = append(cl.inbox, p)
	}
}

func (s *Server) Disconnected(c *transport.Conn, err error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	cl := s.byConn[c]
	if cl == nil {
		return
	}
	delete(s.byConn, c)
	for i, p := range s.pending {
		if p == cl {
			s.pending = append(s.pending[:i], s.pending[i+1:]...)
			return
		}
	}
	for i, a := range s.clients {
		if a == cl {
			s.clients = append(s.clients[:i], s.clients[i+1:]...)
			if !cl.kicked {
				s.lost = append(s.lost, cl.netID)
			}
			return
		}
	}
}

// --- the game's side (session goroutine)

// Update is bb_serverUpdate(delay, UPDATE_SEND_RECV) (baboNet.cpp:260). Sends go out as they are
// queued, so only the receive side remains: a lost client first, then the connection check.
func (s *Server) Update(delay float32) Event {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.connCheck += delay
	if len(s.lost) > 0 {
		id := s.lost[0]
		s.lost = s.lost[1:]
		return Event{Kind: EvLost, NetID: id}
	}
	if s.connCheck >= 0.5 {
		s.connCheck = 0
		if len(s.pending) > 0 {
			cl := s.pending[0]
			s.pending = s.pending[1:]
			s.lastNetID++
			cl.netID = s.lastNetID
			s.clients = append(s.clients, cl)
			return Event{Kind: EvNew, NetID: cl.netID, IP: cl.conn.Remote}
		}
	}
	return Event{}
}

// Receive is bb_serverReceive: the next packet, scanning clients in admission order.
func (s *Server) Receive() (uint32, wire.Packet, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, cl := range s.clients {
		if len(cl.inbox) > 0 {
			p := cl.inbox[0]
			cl.inbox = cl.inbox[1:]
			if s.Trace != nil {
				s.Trace("recv", cl.netID, p)
			}
			return cl.netID, p, true
		}
	}
	return 0, wire.Packet{}, false
}

// Send is bb_serverSend: dest < 1 is every client, else the client with that NetID (unknown: false).
func (s *Server) Send(dest int32, typ uint16, data []byte, proto wire.Protocol) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	p := wire.Packet{Type: typ, Protocol: proto, Data: data}
	if dest < 1 {
		for _, cl := range s.clients {
			s.send(cl, p)
		}
		return true
	}
	for _, cl := range s.clients {
		if cl.netID == uint32(dest) {
			s.send(cl, p)
			return true
		}
	}
	return false
}

func (s *Server) send(cl *client, p wire.Packet) {
	if s.Trace != nil {
		s.Trace("send", cl.netID, p)
	}
	cl.conn.Send(p)
}

// Disconnect is bb_serverDisconnectClient: the connection closes and no lost event follows.
func (s *Server) Disconnect(netID uint32) {
	s.mu.Lock()
	var c *transport.Conn
	for i, cl := range s.clients {
		if cl.netID == netID {
			cl.kicked = true
			c = cl.conn
			s.clients = append(s.clients[:i], s.clients[i+1:]...)
			delete(s.byConn, c)
			break
		}
	}
	s.mu.Unlock()
	if c != nil {
		c.Close("disconnected by the server")
	}
}

// Close drops every connection.
func (s *Server) Close() {
	s.mu.Lock()
	var conns []*transport.Conn
	for c := range s.byConn {
		conns = append(conns, c)
	}
	s.mu.Unlock()
	for _, c := range conns {
		c.Close("server stopped")
	}
}

// Count is the number of admitted clients.
func (s *Server) Count() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.clients)
}
