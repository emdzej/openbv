// Package transport carries baboNet packets over WebSockets: one connection per game client, a
// reader that hands packets to the session and a writer with a bounded queue, so a slow client
// can't hold the game up.
package transport

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"

	"github.com/emdzej/openbv/server/internal/wire"
)

// SendQueue is how many packets may wait for one client; past it the client is dropped (the
// original's TCP send buffers did the same, more slowly).
const SendQueue = 2048

// PingEvery keeps proxies from closing quiet connections.
const PingEvery = 30 * time.Second

// Handler receives what happens on connections. Calls come from each connection's own goroutine.
type Handler interface {
	Connected(c *Conn)
	Received(c *Conn, p wire.Packet)
	Disconnected(c *Conn, err error)
}

// Conn is one client.
type Conn struct {
	ID     uint32 // baboNet's NetID, from 1, unique within the process
	Remote string // the client's address, as the original's playerIP

	ws     *websocket.Conn
	out    chan []byte
	closed atomic.Bool
	once   sync.Once
	cancel context.CancelFunc
	log    *slog.Logger
}

var lastID atomic.Uint32

// Send queues a packet; false if the connection is closed or its queue is full (then it is closed).
func (c *Conn) Send(p wire.Packet) bool {
	if c.closed.Load() {
		return false
	}
	select {
	case c.out <- wire.Encode(p):
		return true
	default:
		c.Close("send queue full")
		return false
	}
}

// Close ends the connection with a reason the client's log shows.
func (c *Conn) Close(reason string) {
	c.once.Do(func() {
		c.closed.Store(true)
		c.cancel()
		// the close handshake can take seconds: never on the caller's (the game's) time
		go c.ws.Close(websocket.StatusNormalClosure, reason)
	})
}

// Accept upgrades an HTTP request and serves the connection until it ends.
func Accept(w http.ResponseWriter, r *http.Request, h Handler, origins []string, log *slog.Logger) {
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{OriginPatterns: origins})
	if err != nil {
		log.Debug("websocket accept", "err", err)
		return
	}
	ws.SetReadLimit(wire.HeaderSize + wire.MaxPayload)
	ctx, cancel := context.WithCancel(context.Background())
	c := &Conn{
		ID:     lastID.Add(1),
		Remote: remoteIP(r),
		ws:     ws,
		out:    make(chan []byte, SendQueue),
		cancel: cancel,
	}
	c.log = log.With("conn", c.ID, "remote", c.Remote)
	h.Connected(c)
	go c.writer(ctx)
	err = c.reader(ctx, h)
	c.Close("")
	h.Disconnected(c, err)
}

func (c *Conn) reader(ctx context.Context, h Handler) error {
	for {
		typ, msg, err := c.ws.Read(ctx)
		if err != nil {
			return err
		}
		if typ != websocket.MessageBinary {
			continue
		}
		p, err := wire.Decode(msg)
		if err != nil {
			return err
		}
		h.Received(c, p)
	}
}

func (c *Conn) writer(ctx context.Context) {
	ping := time.NewTicker(PingEvery)
	defer ping.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case m := <-c.out:
			wctx, cancel := context.WithTimeout(ctx, 10*time.Second)
			err := c.ws.Write(wctx, websocket.MessageBinary, m)
			cancel()
			if err != nil {
				if !errors.Is(err, context.Canceled) {
					c.log.Debug("write", "err", err)
				}
				c.Close("write failed")
				return
			}
		case <-ping.C:
			pctx, cancel := context.WithTimeout(ctx, 10*time.Second)
			err := c.ws.Ping(pctx)
			cancel()
			if err != nil {
				c.Close("ping timeout")
				return
			}
		}
	}
}

// remoteIP is the client's address: the proxy's X-Forwarded-For first entry if set, else the peer.
func remoteIP(r *http.Request) string {
	if f := r.Header.Get("X-Forwarded-For"); f != "" {
		for i := 0; i < len(f); i++ {
			if f[i] == ',' {
				return f[:i]
			}
		}
		return f
	}
	host := r.RemoteAddr
	for i := len(host) - 1; i >= 0; i-- {
		if host[i] == ':' {
			return host[:i]
		}
	}
	return host
}
