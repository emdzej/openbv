package session

import (
	"context"
	"fmt"
	"log/slog"
	"strings"
	"sync"
	"time"
)

// LogLine is one line of a session's log, for the admin page.
type LogLine struct {
	Seq   uint64    `json:"seq"`
	At    time.Time `json:"at"`
	Level string    `json:"level"`
	Text  string    `json:"text"`
}

// LogRing keeps a session's last log lines.
type LogRing struct {
	mu    sync.Mutex
	lines []LogLine
	next  uint64
	keep  int
}

func newLogRing(keep int) *LogRing { return &LogRing{keep: keep, next: 1} }

func (r *LogRing) add(l LogLine) {
	r.mu.Lock()
	defer r.mu.Unlock()
	l.Seq = r.next
	r.next++
	r.lines = append(r.lines, l)
	if len(r.lines) > r.keep {
		r.lines = r.lines[len(r.lines)-r.keep:]
	}
}

// Since returns the lines after seq (0: all kept), oldest first.
func (r *LogRing) Since(seq uint64) []LogLine {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := []LogLine{}
	for _, l := range r.lines {
		if l.Seq > seq {
			out = append(out, l)
		}
	}
	return out
}

// teeHandler passes records on and also writes them to a LogRing (Info and above).
type teeHandler struct {
	next  slog.Handler
	ring  *LogRing
	attrs []slog.Attr
}

func (h *teeHandler) Enabled(ctx context.Context, l slog.Level) bool {
	return l >= slog.LevelInfo || h.next.Enabled(ctx, l)
}

func (h *teeHandler) Handle(ctx context.Context, r slog.Record) error {
	if r.Level >= slog.LevelInfo {
		var b strings.Builder
		b.WriteString(r.Message)
		add := func(a slog.Attr) bool {
			if a.Key == "session" || a.Key == "console" {
				return true
			}
			fmt.Fprintf(&b, " %s=%v", a.Key, a.Value)
			return true
		}
		for _, a := range h.attrs {
			add(a)
		}
		r.Attrs(add)
		h.ring.add(LogLine{At: r.Time, Level: r.Level.String(), Text: b.String()})
	}
	if h.next.Enabled(ctx, r.Level) {
		return h.next.Handle(ctx, r)
	}
	return nil
}

func (h *teeHandler) WithAttrs(as []slog.Attr) slog.Handler {
	return &teeHandler{next: h.next.WithAttrs(as), ring: h.ring, attrs: append(append([]slog.Attr(nil), h.attrs...), as...)}
}

func (h *teeHandler) WithGroup(name string) slog.Handler {
	return &teeHandler{next: h.next.WithGroup(name), ring: h.ring, attrs: h.attrs}
}
