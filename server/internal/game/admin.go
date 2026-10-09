package game

import (
	"crypto/md5"
	"encoding/hex"
	"strings"

	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
)

// voting is Game::SVoting (Game.h:542) on the server.
type voting struct {
	inProgress bool
	from, what string
	yes, no    int
	remaining  float32
	active     []uint32 // the babonetIDs that may vote: players on red or blue when it started
}

// castVote is SVoting::castVote (Game.h:560).
func (s *Server) castVote(m *proto.VoteRequestMsg) {
	v := &s.vote
	v.active = v.active[:0]
	for _, p := range s.players {
		if p == nil {
			continue
		}
		if p.TeamID != proto.TeamAutoAssign && p.TeamID != proto.TeamSpectator {
			v.active = append(v.active, p.BabonetID)
			p.Voted = false
		} else {
			p.Voted = true
		}
	}
	v.what = proto.CString(m.Vote[:])
	v.yes, v.no = 0, 0
	v.remaining = 30
	v.inProgress = true
}

// updateVote is SVoting::update (Game.h:593) and its result (Game.cpp:355): it ends at 30 s, or
// once yes or no is more than half the voters, or everyone voted. It passes with more than half
// yes; then the vote's text runs as a console command.
func (s *Server) updateVote() {
	v := &s.vote
	if !v.inProgress {
		return
	}
	n := len(v.active)
	v.remaining -= delay
	done := false
	if v.remaining <= 0 {
		v.remaining = 0
		done = true
	} else if v.yes > n/2 || v.no > n/2 || v.yes+v.no >= n {
		done = true
	}
	if !done {
		return
	}
	v.inProgress = false
	passed := v.yes > n/2
	var r proto.SvclVoteResultMsg
	if passed {
		r.Passed = 1
	}
	s.broadcast(proto.SvclVoteResult, &r)
	s.out("> Vote " + map[bool]string{true: "passed", false: "failed"}[passed] + ": " + v.what)
	if passed {
		s.exec(v.what, "vote")
	}
}

// voteRequest is NET_CLSV_SVCL_VOTE_REQUEST (ServerRecv.cpp:94). sv_enableVote isn't checked here
// (quirk, design/server.md §5.16): a vote can start when ballots are refused, and then times out.
func (s *Server) voteRequest(p *Player, m *proto.VoteRequestMsg) {
	m.Vote[len(m.Vote)-1] = 0
	if s.vote.inProgress {
		s.out("> Warning, player " + p.Name + " trying to cast a vote, there is already a vote in progress")
		return
	}
	if !s.validateVote(proto.CString(m.Vote[:])) {
		s.out("> Warning, player " + p.Name + " trying to cast an invalid vote")
		return
	}
	for _, o := range s.players {
		if o != nil {
			o.Voted = false
		}
	}
	s.castVote(m)
	s.vote.from = colorLess(p.Name)
	s.broadcast(proto.ClsvSvclVoteRequest, m)
	s.out("> " + s.vote.from + " started a vote: " + s.vote.what)
}

// ballot is NET_CLSV_VOTE (ServerRecv.cpp:60).
func (s *Server) ballot(p *Player, m *proto.ClsvVoteMsg) {
	if !s.SV.EnableVote.B || p.Voted {
		return
	}
	active := false
	for _, id := range s.vote.active {
		if id == p.BabonetID {
			active = true
			break
		}
	}
	if !active {
		return
	}
	p.Voted = true
	if m.Value != 0 {
		s.vote.yes++
	} else {
		s.vote.no++
	}
	s.broadcast(proto.SvclUpdateVote, &proto.SvclUpdateVoteMsg{NbYes: int8(s.vote.yes), NbNo: int8(s.vote.no)})
}

// validateVote is Server::validateVote (Server.cpp:123): the command, or for "set sv_x ..." the
// variable, lower-cased, must have been allowed with voteon.
func (s *Server) validateVote(vote string) bool {
	f := strings.Fields(vote)
	if len(f) == 0 {
		return false
	}
	command := f[0]
	if len(vote) >= 7 && strings.EqualFold(vote[:7], "set sv_") && len(f) > 1 {
		command = f[1]
	}
	command = strings.ToLower(command)
	for _, c := range s.voteList {
		if c == command {
			return true
		}
	}
	return false
}

// cancelVoteFor is the vote check when a player leaves (Server.cpp:497): a vote to kick or ban him
// fails at once. The vote itself stays in progress until it times out (quirk, §5.16).
func (s *Server) cancelVoteFor(i int, p *Player) {
	if !s.vote.inProgress {
		return
	}
	command, rest, _ := strings.Cut(s.vote.what, " ")
	fail := false
	switch {
	case strings.EqualFold(command, "kick") || strings.EqualFold(command, "ban"):
		fail = strings.EqualFold(rest, colorLess(p.Name))
	case strings.EqualFold(command, "kickid") || strings.EqualFold(command, "banid"):
		fail = atoi(rest) == i
	}
	if fail {
		s.broadcast(proto.SvclVoteResult, &proto.SvclVoteResultMsg{Passed: 0})
	}
}

// adminRequest is NET_CLSV_ADMIN_REQUEST (ServerRecv.cpp:186, the Pro build): the client sends the
// MD5 hex of the login and the password (Console.cpp:706, "admin <login> <password>"); they are
// compared with the MD5s of zsv_adminUser and zsv_adminPass, case-insensitively (CString ==). An
// empty login or password logs the player out. Without both variables set, nothing happens.
func (s *Server) adminRequest(netID uint32, m *proto.ClsvAdminRequestMsg) {
	if s.SV.AdminUser.S == "" || s.SV.AdminPass.S == "" {
		return
	}
	var p *Player
	for _, o := range s.players {
		if o != nil && o.BabonetID == netID {
			p = o
			break
		}
	}
	if p == nil {
		return
	}
	login, pwd := proto.CString(m.Login[:]), proto.CString(m.Password[:])
	if login == "" || pwd == "" {
		p.IsAdmin = false
		return
	}
	if strings.EqualFold(login, md5hex(s.SV.AdminUser.S)) && strings.EqualFold(pwd, md5hex(s.SV.AdminPass.S)) {
		s.sendEmpty(int32(netID), proto.SvclAdminAccepted)
		p.IsAdmin = true
		s.out("Admin name: " + p.Name)
		s.out("Admin IP: " + p.IP)
	} else {
		s.out("Admin log-in attempt by: " + p.Name)
		s.out("Admin log-in attempt IP: " + p.IP)
	}
}

func md5hex(s string) string {
	h := md5.Sum([]byte(s))
	return hex.EncodeToString(h[:])
}

// adminCommand is NET_SVCL_CONSOLE from a client (ServerRecv.cpp:163): an admin's console command.
func (s *Server) adminCommand(netID uint32, text string) {
	for _, p := range s.players {
		if p != nil && p.BabonetID == netID && p.IsAdmin {
			s.out("Executing command from " + p.Name + ", IP: " + p.IP)
			s.exec(text, "admin "+colorLess(p.Name))
			return
		}
	}
}

// out is console->add(message, true) on a server (Console.cpp:108): the server's log, the admin
// page's log, the output of the command being run, and every in-game admin as ">> message"
// (NET_SVCL_CONSOLE, the text with its terminating NUL).
func (s *Server) out(msg string) {
	clean := colorLess(msg)
	s.log.Info(clean, "console", true)
	if s.capture != nil {
		*s.capture = append(*s.capture, clean)
	}
	payload := append([]byte(">> "+msg), 0)
	for _, p := range s.players {
		if p != nil && p.IsAdmin {
			s.net.Send(int32(p.BabonetID), proto.SvclConsole, payload, 0)
		}
	}
}

// colorLess is textColorLess (Helper.cpp:28): without the colour codes (bytes below 0x10, except
// the newline).
func colorLess(s string) string {
	var b strings.Builder
	for i := 0; i < len(s); i++ {
		if s[i] >= 0x10 || s[i] == '\n' {
			b.WriteByte(s[i])
		}
	}
	return b.String()
}

// Control is what a session lets its game do to it (set by the session manager).
type Control = session.Control

// SetControl gives the game its session's hooks: restart, stop, and saving changed settings.
func (s *Server) SetControl(c session.Control) { s.control = c }

// changed tells the session what the console changed, so a restart (or the next boot) keeps it.
func (s *Server) changed() {
	if s.control.Changed == nil {
		return
	}
	cvars := map[string]string{}
	for k, v := range s.setCvars {
		cvars[k] = v
	}
	s.control.Changed(session.Changes{Cvars: cvars, Maps: append([]string(nil), s.mapList...),
		VoteOn: append([]string(nil), s.voteList...)})
}

// banned reports whether a connection's address or a player's MAC is on the ban list.
func (s *Server) banned(ip, mac string) (store.Ban, bool) {
	if s.bans == nil {
		return store.Ban{}, false
	}
	return s.bans.Banned(ip, mac)
}
