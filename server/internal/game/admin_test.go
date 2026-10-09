package game

import (
	"strings"
	"testing"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
)

// adminServer: a DM server with three players (two playing, one watching) and a recorded output.
func adminServer(t *testing.T, st session.Settings) (*Server, *[]sentMsg) {
	needContent(t)
	if st.Name == "" {
		st.Name = "test"
	}
	if len(st.Maps) == 0 {
		st.Maps = []string{"DM-MiniArena"}
	}
	bans, _ := store.OpenBans("")
	s, err := New(st, content, bbnet.New(), quiet, bans)
	if err != nil {
		t.Fatal(err)
	}
	var log []sentMsg
	s.sent = func(dest int32, typ uint16, msg any) { log = append(log, sentMsg{dest, typ, msg}) }
	for i, team := range []int{proto.TeamBlue, proto.TeamRed, proto.TeamSpectator} {
		p := newPlayer(i, uint32(i+1), 5)
		p.TeamID = team
		p.Name = []string{"Alpha", "\x04Bravo", "Charlie"}[i]
		p.IP = []string{"10.0.0.1", "10.0.0.2", "10.0.0.3"}[i]
		p.MAC = []string{"02-00-00-00-00-01", "02-00-00-00-00-02", "02-00-00-00-00-03"}[i]
		s.players[i] = p
	}
	return s, &log
}

func voteRequest(text string, id int8) *proto.VoteRequestMsg {
	m := &proto.VoteRequestMsg{PlayerID: id}
	copy(m.Vote[:], text)
	return m
}

func voteFrames(s *Server, n int) {
	for i := 0; i < n; i++ {
		s.updateVote()
	}
}

// Only commands allowed with voteon can be voted on; for set, the variable is what counts.
func TestValidateVote(t *testing.T) {
	s, _ := adminServer(t, session.Settings{VoteOn: []string{"Kick", "sv_scoreLimit"}})
	for vote, want := range map[string]bool{
		"kick Bravo": true, "KICK x": true, "ban Bravo": false, "set sv_scoreLimit 10": true,
		"set sv_gameType 1": false, "sv_scoreLimit 3": true, "": false,
	} {
		if got := s.validateVote(vote); got != want {
			t.Errorf("%q: %v, want %v", vote, got, want)
		}
	}
}

// A vote passes with more than half of the red/blue players voting yes, then runs as a command.
func TestVotePasses(t *testing.T) {
	s, log := adminServer(t, session.Settings{VoteOn: []string{"sv_scoreLimit"}})
	s.SV.ScoreLimit.I = 50
	s.voteRequest(s.players[0], voteRequest("set sv_scoreLimit 10", 0))
	if !s.vote.inProgress || len(s.vote.active) != 2 {
		t.Fatalf("vote: %+v", s.vote)
	}
	if count(*log, proto.ClsvSvclVoteRequest) != 1 {
		t.Fatal("the request isn't echoed")
	}
	s.ballot(s.players[2], &proto.ClsvVoteMsg{Value: 1, PlayerID: 2}) // a spectator: not counted
	s.ballot(s.players[0], &proto.ClsvVoteMsg{Value: 1, PlayerID: 0})
	s.ballot(s.players[0], &proto.ClsvVoteMsg{Value: 1, PlayerID: 0}) // twice: once
	if s.vote.yes != 1 || count(*log, proto.SvclUpdateVote) != 1 {
		t.Fatalf("yes %d, updates %d", s.vote.yes, count(*log, proto.SvclUpdateVote))
	}
	s.ballot(s.players[1], &proto.ClsvVoteMsg{Value: 1, PlayerID: 1})
	voteFrames(s, 1)
	if s.vote.inProgress {
		t.Fatal("still in progress with everyone voted")
	}
	var r *proto.SvclVoteResultMsg
	for _, m := range *log {
		if m.typ == proto.SvclVoteResult {
			r = m.msg.(*proto.SvclVoteResultMsg)
		}
	}
	if r == nil || r.Passed != 1 {
		t.Fatalf("result %+v", r)
	}
	if s.SV.ScoreLimit.I != 10 {
		t.Fatalf("the command didn't run: sv_scoreLimit %d", s.SV.ScoreLimit.I)
	}
}

// Ballots are refused with sv_enableVote off, but a vote can still start and then times out (quirk).
func TestVoteTimesOutWhenDisabled(t *testing.T) {
	s, log := adminServer(t, session.Settings{VoteOn: []string{"kick"}})
	s.SV.EnableVote.B = false
	s.voteRequest(s.players[0], voteRequest("kick Charlie", 0))
	s.ballot(s.players[1], &proto.ClsvVoteMsg{Value: 1, PlayerID: 1})
	if s.vote.yes != 0 {
		t.Fatal("a ballot counted with voting disabled")
	}
	voteFrames(s, 30*30+2) // 900 steps of 1/30 in float32 leave a little: the original's too
	if s.vote.inProgress {
		t.Fatal("no timeout after 30 s")
	}
	if s.players[2] == nil || count(*log, proto.SvclVoteResult) != 1 {
		t.Fatal("a failed vote kicked, or no result")
	}
}

// A second vote can't start while one runs; a kick vote fails when its target leaves.
func TestVoteCancelledWhenTargetLeaves(t *testing.T) {
	s, log := adminServer(t, session.Settings{VoteOn: []string{"kickid"}})
	s.voteRequest(s.players[0], voteRequest("kickid 1", 0))
	s.voteRequest(s.players[1], voteRequest("kickid 0", 1))
	if s.vote.what != "kickid 1" {
		t.Fatalf("the second vote replaced the first: %q", s.vote.what)
	}
	s.cancelVoteFor(1, s.players[1])
	if count(*log, proto.SvclVoteResult) != 1 || !s.vote.inProgress {
		t.Fatal("expected an immediate failed result, the vote still in progress (quirk)")
	}
}

// The in-game admin login compares MD5 hex digests, case-insensitively; admins get console output.
func TestAdminLogin(t *testing.T) {
	s, log := adminServer(t, session.Settings{AdminUser: "boss", AdminPassword: "s3cret"})
	req := func(login, pwd string) *proto.ClsvAdminRequestMsg {
		m := &proto.ClsvAdminRequestMsg{}
		copy(m.Login[:], login)
		copy(m.Password[:], pwd)
		return m
	}
	s.adminRequest(1, req(md5hex("boss"), md5hex("wrong")))
	if s.players[0].IsAdmin {
		t.Fatal("a wrong password logged in")
	}
	s.adminRequest(1, req(strings.ToUpper(md5hex("boss")), md5hex("s3cret")))
	if !s.players[0].IsAdmin || count(*log, proto.SvclAdminAccepted) != 1 {
		t.Fatal("the right password didn't log in")
	}
	// an admin's command runs; someone else's doesn't
	s.adminCommand(2, "set sv_scoreLimit 7")
	if s.SV.ScoreLimit.I == 7 {
		t.Fatal("a non-admin's command ran")
	}
	s.adminCommand(1, "set sv_scoreLimit 7")
	if s.SV.ScoreLimit.I != 7 {
		t.Fatal("the admin's command didn't run")
	}
	s.adminRequest(1, req("", ""))
	if s.players[0].IsAdmin {
		t.Fatal("an empty login doesn't log out")
	}
}

// set: sv_* go to every client after validation, zsv_* never; the password is cut to 15
// characters and never sent; what's set is reported to the session.
func TestSetCommand(t *testing.T) {
	s, log := adminServer(t, session.Settings{})
	var changes []session.Changes
	s.SetControl(session.Control{Changed: func(c session.Changes) { changes = append(changes, c) }})
	out := s.Exec("set sv_scoreLimit 25", "test")
	if s.SV.ScoreLimit.I != 25 || count(*log, proto.SvclSvChange) != 1 || len(out) == 0 {
		t.Fatalf("set: %d, changes sent %d, output %v", s.SV.ScoreLimit.I, count(*log, proto.SvclSvChange), out)
	}
	s.Exec("set sv_maxPlayer 99", "test") // out of range: refused, nothing sent
	if s.SV.MaxPlayer.I == 99 || count(*log, proto.SvclSvChange) != 1 {
		t.Fatal("an out-of-range value was set or sent")
	}
	s.Exec("set zsv_adminPass hunter2", "test")
	if s.SV.AdminPass.S != "hunter2" || count(*log, proto.SvclSvChange) != 1 {
		t.Fatal("zsv_adminPass sent to clients")
	}
	s.Exec("set sv_password 0123456789abcdefXYZ", "test")
	if s.SV.Password.S != "0123456789abcde" {
		t.Fatalf("password %q", s.SV.Password.S)
	}
	for _, m := range *log {
		if m.typ == proto.SvclSvChange && strings.Contains(proto.CString(m.msg.(*proto.SvclSvChangeMsg).SvChange[:]), "0123") {
			t.Fatal("the password was sent")
		}
	}
	if len(changes) == 0 || changes[len(changes)-1].Cvars["sv_scoreLimit"] != "25" {
		t.Fatalf("changes %+v", changes)
	}
	if s.Exec("set cl_cubicMotion false", "test"); !s.SV.CubicMotion.B {
		t.Fatal("a client variable was set from the server console")
	}
}

// ban: the player goes onto the shared list (IP and MAC) and is disconnected; banned addresses and
// MACs can't come back; unban takes them off.
func TestBans(t *testing.T) {
	s, log := adminServer(t, session.Settings{})
	s.Exec("banid 1", "admin")
	if s.players[1] != nil || count(*log, proto.SvclPlayerDisconnect) != 1 {
		t.Fatal("banned player still here")
	}
	if _, ok := s.banned("10.0.0.2", ""); !ok {
		t.Fatal("the IP isn't banned")
	}
	if _, ok := s.banned("", "02-00-00-00-00-02"); !ok {
		t.Fatal("the MAC isn't banned")
	}
	if b := s.bans.List()[0]; b.Name != "Bravo" || b.By != "admin" {
		t.Fatalf("ban entry %+v", b)
	}
	s.Exec("banip 192.168.1.1", "admin")
	out := s.Exec("banlist", "admin")
	if len(out) != 2 || !strings.Contains(out[1], "192.168.1.1") {
		t.Fatalf("banlist %v", out)
	}
	s.Exec("unban 0", "admin")
	if _, ok := s.banned("10.0.0.2", ""); ok {
		t.Fatal("unban didn't remove the ban")
	}
	s.Exec("kick charlie", "admin") // names compare without colours, case-insensitively
	if s.players[2] != nil {
		t.Fatal("kick by name didn't kick")
	}
}

// The rotation: addmap only existing maps, once; removemap never the current map.
func TestRotationCommands(t *testing.T) {
	s, _ := adminServer(t, session.Settings{})
	s.Exec("addmap DM-Arena", "a")
	s.Exec("addmap DM-Arena", "a")
	s.Exec("addmap No-Such-Map", "a")
	if strings.Join(s.mapList, ",") != "DM-MiniArena,DM-Arena" {
		t.Fatalf("rotation %v", s.mapList)
	}
	s.Exec("removemap DM-MiniArena", "a")
	if len(s.mapList) != 2 {
		t.Fatal("the current map was removed")
	}
	s.Exec("removemap dm-arena", "a")
	if len(s.mapList) != 1 {
		t.Fatalf("rotation %v", s.mapList)
	}
	s.Exec("changemap DM-Arena", "a")
	if s.nextMap != "DM-Arena" || s.roundState != proto.GameMapChange || s.changeMapDelay != 10 {
		t.Fatalf("changemap: next %q, state %d, delay %v", s.nextMap, s.roundState, s.changeMapDelay)
	}
}

// moveid -1 moves everyone; move by name; an unknown command says so.
func TestMoveAndUnknown(t *testing.T) {
	s, _ := adminServer(t, session.Settings{})
	s.Exec("move 1 alpha", "a")
	if s.players[0].TeamID != proto.TeamRed {
		t.Fatalf("move: team %d", s.players[0].TeamID)
	}
	s.Exec("moveid -1 -1", "a")
	for _, p := range s.players[:3] {
		if p.TeamID != proto.TeamSpectator {
			t.Fatalf("moveid -1: %s on %d", p.Name, p.TeamID)
		}
	}
	if out := s.Exec("frobnicate", "a"); len(out) == 0 || !strings.Contains(out[0], "frobnicate") {
		t.Fatalf("unknown command: %v", out)
	}
}

// Settings: variables, the admin login, votable commands and privacy are applied; unknown or
// invalid variables refuse to start.
func TestSettingsApplied(t *testing.T) {
	s, _ := adminServer(t, session.Settings{Cvars: map[string]string{"sv_scoreLimit": "12", "sv_friendlyFire": "true"},
		AdminUser: "u", AdminPassword: "p", VoteOn: []string{" Kick "}, Private: true})
	if s.SV.ScoreLimit.I != 12 || !s.SV.FriendlyFire.B || s.SV.AdminUser.S != "u" || s.SV.GamePublic.B || s.voteList[0] != "kick" {
		t.Fatal("settings not applied")
	}
	if st := s.Status(); st.Public || st.Players != 3 || st.Map != "DM-MiniArena" {
		t.Fatalf("status %+v", st)
	}
	for _, c := range s.Cvars() {
		if c.Name == "zsv_adminPass" && c.Value == "p" {
			t.Fatal("the admin password is shown")
		}
	}
	needContent(t)
	for _, bad := range []map[string]string{{"sv_nope": "1"}, {"sv_scoreLimit": "x"}, {"cl_cubicMotion": "false"}} {
		if _, err := New(session.Settings{Name: "x", Maps: []string{"DM-MiniArena"}, Cvars: bad}, content, bbnet.New(), quiet, nil); err == nil {
			t.Errorf("%v started", bad)
		}
	}
}
